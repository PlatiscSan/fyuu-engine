module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <algorithm>
#include <iterator>

#include <string>
#include <limits>

#include <cstdint>
#include <unordered_set>

#include <optional>
#include <string_view>

#include <ranges>
#include <span>
#include <format>
#endif // !defined(__cpp_lib_modules)
#include <vulkan/vulkan.h>
#if !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)
#define FYUU_RHI_USE_VULKAN_HEADER
#include <vulkan/vulkan_shared.hpp>
#endif // !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)
#if !defined(__APPLE__)
#include <vma/vk_mem_alloc.h>
#endif // !defined(__APPLE__)

module fyuu_rhi:vulkan_physical_device;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
#if !defined(FYUU_RHI_USE_VULKAN_HEADER)
import vulkan;
#endif // !defined(FYUU_RHI_USE_VULKAN_HEADER)
import :log;
import :logical_device_factory;
import :physical_device_dispatch;
import :physical_device_factory;
import :vulkan_data;
import :vulkan_memory_allocator;
import :vulkan_queue_allocator;
#endif // !defined(__APPLE__)

#if !defined(__APPLE__)
namespace {

	fyuu_rhi::PhysicalDevice::Info::Type GetPhysicalDeviceType(vk::PhysicalDeviceType type) noexcept {
		using Type = fyuu_rhi::PhysicalDevice::Info::Type;
		switch (type) {
		case vk::PhysicalDeviceType::eDiscreteGpu:
			return Type::DiscreteGPU;
		case vk::PhysicalDeviceType::eIntegratedGpu:
			return Type::IntegratedGPU;
		case vk::PhysicalDeviceType::eVirtualGpu:
			return Type::Virtual;
		case vk::PhysicalDeviceType::eCpu:
			return Type::CPU;
		default:
			return Type::Unknown;
		}
	}

	/// Enumerates every non-empty queue family of the physical device so the
	/// immutable queue pool can be built before device creation (Vulkan freezes
	/// queue counts and priorities at that point).
	std::vector<fyuu_rhi::vulkan::CommandQueueInfo> QueryQueueInfo(fyuu_rhi::vulkan::PhysicalDevice const* phys_dev) {
		auto queue_families = phys_dev->impl->getQueueFamilyProperties(*(phys_dev->dispatcher));
		std::vector<fyuu_rhi::vulkan::CommandQueueInfo> queue_infos;
		for (std::uint32_t family = 0u; family < queue_families.size(); ++family) {
			auto const& properties = queue_families[family];
			fyuu_rhi::vulkan::CommandQueueType type =
				fyuu_rhi::vulkan::CommandQueueType::None;
			if (properties.queueFlags & vk::QueueFlagBits::eGraphics) {
				type |= fyuu_rhi::vulkan::CommandQueueType::Graphics;
			}
			if (properties.queueFlags & vk::QueueFlagBits::eCompute) {
				type |= fyuu_rhi::vulkan::CommandQueueType::Compute;
			}
			if (properties.queueFlags & vk::QueueFlagBits::eTransfer) {
				type |= fyuu_rhi::vulkan::CommandQueueType::Copy;
			}
			if (properties.queueCount != 0u) {
				queue_infos.emplace_back(
					fyuu_rhi::vulkan::CommandQueueInfo{
						type,
						family,
						properties.queueCount
					}
				);
			}
		}
		return queue_infos;
	}

	/// Enables one optional device extension unless it has been promoted into the
	/// core API at the given device version. A missing extension is logged as a
	/// warning rather than failing device creation; extensions are added on
	/// demand and the engine degrades gracefully without them.
	void AddOptionalExtension(
		std::uint32_t device_api_version,
		std::unordered_set<std::string_view> const& available_extensions,
		std::vector<char const*>& enabled_extensions,
		std::string_view ext_name,
		std::uint32_t core_version
	) {
		if (device_api_version >= core_version) {
			return;
		}
		if (!available_extensions.contains(ext_name)) {
			fyuu_rhi::log::Warning(
				std::format(
					"Vulkan device extension '{}' is unavailable",
					ext_name
				)
			);
			return;
		}
		enabled_extensions.emplace_back(ext_name.data());
	}

	/// Enables a mandatory device extension, failing device creation when the
	/// device does not expose it.
	void AddMandatoryExtension(
		std::uint32_t device_api_version,
		std::unordered_set<std::string_view> const& available_extensions,
		std::vector<char const*>& enabled_extensions,
		std::string_view ext_name,
		std::uint32_t core_version
	) {
		if (device_api_version >= core_version) {
			return;
		}
		if (!available_extensions.contains(ext_name)) {
			throw std::runtime_error(
				std::format(
					"Mandatory Vulkan device extension '{}' is unavailable",
					ext_name
				)
			);
		}
		enabled_extensions.emplace_back(ext_name.data());
	}

} // namespace

namespace fyuu_rhi {

	template <>
	struct GetPhysicalDeviceInfo<vulkan::PhysicalDevice> {
		vulkan::PhysicalDevice const* native;

		PhysicalDevice::Info operator()() const {
			auto properties = native->impl->getProperties(
				*native->dispatcher
			);
			auto memory_properties = native->impl->getMemoryProperties(
				*native->dispatcher
			);
			std::size_t dedicated_memory = 0u;
			auto heaps = std::span(
				memory_properties.memoryHeaps.data(),
				memory_properties.memoryHeapCount
			) | std::views::filter(
				[](auto const& heap) {
					return static_cast<bool>(
						heap.flags & vk::MemoryHeapFlagBits::eDeviceLocal
					);
				}
			);
			std::ranges::for_each(
				heaps,
				[&dedicated_memory](auto const& heap) {
					dedicated_memory += static_cast<std::size_t>(heap.size);
				}
			);
			return {
				.name = properties.deviceName.data(),
				.vendor_id = properties.vendorID,
				.device_id = properties.deviceID,
				.dedicated_memory = dedicated_memory,
				.type = GetPhysicalDeviceType(properties.deviceType)
			};
		}
	};

	template <>
	struct CreateLogicalDevice<vulkan::PhysicalDevice> {
		vulkan::PhysicalDevice const* physical_device;

		LogicalDevice operator()() const {
			// Phase 1: build the immutable queue pool from the physical device's
			// queue families. Later phases (device extensions, features, device
			// creation, memory allocator) are migrated incrementally; the assembly
			// throws until they land.
			vulkan::QueueAllocator queue_alloc(QueryQueueInfo(physical_device));

			auto supported_extensions =
				physical_device->impl->enumerateDeviceExtensionProperties(nullptr, *(physical_device->dispatcher)) |
				std::views::transform(
					[](vk::ExtensionProperties const& prop) -> std::string_view {
						return prop.extensionName;
					}
				) |
				std::ranges::to<std::unordered_set>();

			// Phase 2: select device extensions. Everything is added on demand and a
			// missing extension only logs a warning, so the engine degrades gracefully.
			auto device_version = physical_device->impl->getProperties(*(physical_device->dispatcher)).apiVersion;
			std::vector<char const*> enabled_extensions;

			// Presentation is the core workload; a device without swapchain fails here.
			AddMandatoryExtension(device_version, supported_extensions, enabled_extensions, vk::KHRSwapchainExtensionName, (std::numeric_limits<std::uint32_t>::max)());

			// Perf-critical features promoted into core 1.2/1.3.
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::KHRTimelineSemaphoreExtensionName, vk::ApiVersion12);
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::EXTDescriptorIndexingExtensionName, vk::ApiVersion12);
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::KHRBufferDeviceAddressExtensionName, vk::ApiVersion12);
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::KHRDynamicRenderingExtensionName, vk::ApiVersion13);
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::KHRSynchronization2ExtensionName, vk::ApiVersion13);
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::EXTExtendedDynamicStateExtensionName, vk::ApiVersion13);
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::EXTExtendedDynamicState2ExtensionName, vk::ApiVersion13);
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::KHRMaintenance4ExtensionName, vk::ApiVersion13);
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::EXTPipelineCreationCacheControlExtensionName, vk::ApiVersion13);
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::EXTPipelineCreationFeedbackExtensionName, vk::ApiVersion13);
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::EXTHostQueryResetExtensionName, vk::ApiVersion12);
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::KHRShaderSubgroupExtendedTypesExtensionName, vk::ApiVersion12);

			// Swapchain / present chain; the instance-gated extensions only when the
			// matching instance extension was actually enabled.
			if (physical_device->enabled_instance_extensions.contains(vk::EXTSurfaceMaintenance1ExtensionName)) {
				AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::EXTSwapchainMaintenance1ExtensionName, (std::numeric_limits<std::uint32_t>::max)());
			}
			if (supported_extensions.contains(vk::KHRPresentIdExtensionName)) {
				AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::KHRPresentIdExtensionName, (std::numeric_limits<std::uint32_t>::max)());
				AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::KHRPresentWaitExtensionName, (std::numeric_limits<std::uint32_t>::max)());
			}
			if (physical_device->enabled_instance_extensions.contains(vk::KHRGetSurfaceCapabilities2ExtensionName)) {
				AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::KHRPresentModeFifoLatestReadyExtensionName, (std::numeric_limits<std::uint32_t>::max)());
			}

			// Ray tracing: never promoted to core, enabled via the KHR extension set.
			if (supported_extensions.contains(vk::KHRAccelerationStructureExtensionName)) {
				AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::KHRAccelerationStructureExtensionName, (std::numeric_limits<std::uint32_t>::max)());
				AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::KHRRayTracingPipelineExtensionName, (std::numeric_limits<std::uint32_t>::max)());
				AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::KHRRayQueryExtensionName, (std::numeric_limits<std::uint32_t>::max)());
				AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::KHRDeferredHostOperationsExtensionName, (std::numeric_limits<std::uint32_t>::max)());
			}

			// Mesh shaders (never promoted to core).
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::EXTMeshShaderExtensionName, (std::numeric_limits<std::uint32_t>::max)());

			// Memory management: budget queries drive the allocator and priority hints
			// order eviction (VMA consumes the budget extension when available).
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::EXTMemoryBudgetExtensionName, (std::numeric_limits<std::uint32_t>::max)());
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::EXTMemoryPriorityExtensionName, (std::numeric_limits<std::uint32_t>::max)());

			// Shader objects avoid pipeline-object state churn; push descriptors avoid
			// descriptor-set updates for small bindings.
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::EXTShaderObjectExtensionName, (std::numeric_limits<std::uint32_t>::max)());
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::KHRPushDescriptorExtensionName, (std::numeric_limits<std::uint32_t>::max)());

			// Tooling: shader statistics for performance analysis.
			AddOptionalExtension(device_version, supported_extensions, enabled_extensions, vk::KHRPipelineExecutablePropertiesExtensionName, (std::numeric_limits<std::uint32_t>::max)());

			// Phase 3: probe and enable device features. A candidate feature struct is
			// chained into PhysicalDeviceFeatures2 only when its extension (or the core
			// version that promoted it) is available; getFeatures2 then fills the bools
			// with what the device actually supports. enabled_features records what
			// survived, so runtime code can branch on it later.
			auto IsDeviceExtensionEnabled = [&](std::string_view extension) {
				return std::ranges::find_if(
					enabled_extensions,
					[extension](char const* name) {
						return std::string_view(name) == extension;
					}
				) != enabled_extensions.end();
				};

			std::vector<std::pair<vk::StructureType, vk::Bool32*>> probed;
			void* feature_chain = nullptr;
			auto AddFeature = [&](auto& feature, bool available, vk::StructureType type, vk::Bool32* supported) {
				if (!available) {
					return;
				}
				feature.pNext = feature_chain;
				feature_chain = &feature;
				probed.emplace_back(type, supported);
			};
			auto Enable = [&](bool available, vk::Bool32& field, vk::StructureType type, bool& any_available) {
				if (!available) {
					return;
				}
				field = vk::True;
				any_available = true;
				probed.emplace_back(type, &field);
				};

			// Core 1.2 features, consolidated into the promoted struct. This header's
			// PhysicalDeviceDescriptorIndexingFeatures is missing its headline field, so
			// descriptor indexing (and the other 1.2 core features) is enabled here.
			vk::PhysicalDeviceVulkan12Features vulkan12;
			bool vulkan12_available = false;
			Enable(device_version >= vk::ApiVersion12 || IsDeviceExtensionEnabled(vk::KHRTimelineSemaphoreExtensionName), vulkan12.timelineSemaphore, vk::StructureType::ePhysicalDeviceTimelineSemaphoreFeatures, vulkan12_available);
			Enable(device_version >= vk::ApiVersion12 || IsDeviceExtensionEnabled(vk::EXTDescriptorIndexingExtensionName), vulkan12.descriptorIndexing, vk::StructureType::ePhysicalDeviceDescriptorIndexingFeatures, vulkan12_available);
			Enable(device_version >= vk::ApiVersion12 || IsDeviceExtensionEnabled(vk::KHRBufferDeviceAddressExtensionName), vulkan12.bufferDeviceAddress, vk::StructureType::ePhysicalDeviceBufferDeviceAddressFeatures, vulkan12_available);
			Enable(device_version >= vk::ApiVersion12 || IsDeviceExtensionEnabled(vk::EXTHostQueryResetExtensionName), vulkan12.hostQueryReset, vk::StructureType::ePhysicalDeviceHostQueryResetFeatures, vulkan12_available);
			Enable(device_version >= vk::ApiVersion12 || IsDeviceExtensionEnabled(vk::KHRShaderSubgroupExtendedTypesExtensionName), vulkan12.shaderSubgroupExtendedTypes, vk::StructureType::ePhysicalDeviceShaderSubgroupExtendedTypesFeatures, vulkan12_available);
			if (vulkan12_available) {
				vulkan12.pNext = feature_chain;
				feature_chain = &vulkan12;
			}

			// Core 1.3 features consolidated into the promoted struct.
			vk::PhysicalDeviceVulkan13Features vulkan13;
			bool vulkan13_available = false;
			Enable(device_version >= vk::ApiVersion13 || IsDeviceExtensionEnabled(vk::KHRDynamicRenderingExtensionName), vulkan13.dynamicRendering, vk::StructureType::ePhysicalDeviceDynamicRenderingFeatures, vulkan13_available);
			Enable(device_version >= vk::ApiVersion13 || IsDeviceExtensionEnabled(vk::KHRSynchronization2ExtensionName), vulkan13.synchronization2, vk::StructureType::ePhysicalDeviceSynchronization2Features, vulkan13_available);
			Enable(device_version >= vk::ApiVersion13 || IsDeviceExtensionEnabled(vk::KHRMaintenance4ExtensionName), vulkan13.maintenance4, vk::StructureType::ePhysicalDeviceMaintenance4Features, vulkan13_available);
			Enable(device_version >= vk::ApiVersion13 || IsDeviceExtensionEnabled(vk::EXTPipelineCreationCacheControlExtensionName), vulkan13.pipelineCreationCacheControl, vk::StructureType::ePhysicalDevicePipelineCreationCacheControlFeatures, vulkan13_available);
			if (vulkan13_available) {
				vulkan13.pNext = feature_chain;
				feature_chain = &vulkan13;
			}

			// Extended dynamic state is core 1.3 in the spec but not folded into
			// Vulkan13Features in this header; its EXT struct enables the feature.
			vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT extended_dynamic_state{ vk::True };
			AddFeature(extended_dynamic_state, device_version >= vk::ApiVersion13 || IsDeviceExtensionEnabled(vk::EXTExtendedDynamicStateExtensionName), vk::StructureType::ePhysicalDeviceExtendedDynamicStateFeaturesEXT, &extended_dynamic_state.extendedDynamicState);
			vk::PhysicalDeviceExtendedDynamicState2FeaturesEXT extended_dynamic_state2{ vk::True };
			AddFeature(extended_dynamic_state2, device_version >= vk::ApiVersion13 || IsDeviceExtensionEnabled(vk::EXTExtendedDynamicState2ExtensionName), vk::StructureType::ePhysicalDeviceExtendedDynamicState2FeaturesEXT, &extended_dynamic_state2.extendedDynamicState2);

			// Swapchain / present features.
			vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR swapchain_maintenance1{ vk::True };
			AddFeature(swapchain_maintenance1, IsDeviceExtensionEnabled(vk::EXTSwapchainMaintenance1ExtensionName), vk::StructureType::ePhysicalDeviceSwapchainMaintenance1FeaturesKHR, &swapchain_maintenance1.swapchainMaintenance1);
			vk::PhysicalDevicePresentIdFeaturesKHR present_id{ vk::True };
			AddFeature(present_id, IsDeviceExtensionEnabled(vk::KHRPresentIdExtensionName), vk::StructureType::ePhysicalDevicePresentIdFeaturesKHR, &present_id.presentId);
			vk::PhysicalDevicePresentWaitFeaturesKHR present_wait{ vk::True };
			AddFeature(present_wait, IsDeviceExtensionEnabled(vk::KHRPresentWaitExtensionName), vk::StructureType::ePhysicalDevicePresentWaitFeaturesKHR, &present_wait.presentWait);
			vk::PhysicalDevicePresentModeFifoLatestReadyFeaturesKHR fifo_latest_ready{ vk::True };
			AddFeature(fifo_latest_ready, IsDeviceExtensionEnabled(vk::KHRPresentModeFifoLatestReadyExtensionName), vk::StructureType::ePhysicalDevicePresentModeFifoLatestReadyFeaturesKHR, &fifo_latest_ready.presentModeFifoLatestReady);

			// Ray tracing / mesh shader / memory / shader object features.
			vk::PhysicalDeviceAccelerationStructureFeaturesKHR acceleration_structure{ vk::True };
			AddFeature(acceleration_structure, IsDeviceExtensionEnabled(vk::KHRAccelerationStructureExtensionName), vk::StructureType::ePhysicalDeviceAccelerationStructureFeaturesKHR, &acceleration_structure.accelerationStructure);
			vk::PhysicalDeviceRayTracingPipelineFeaturesKHR ray_tracing_pipeline{ vk::True };
			AddFeature(ray_tracing_pipeline, IsDeviceExtensionEnabled(vk::KHRRayTracingPipelineExtensionName), vk::StructureType::ePhysicalDeviceRayTracingPipelineFeaturesKHR, &ray_tracing_pipeline.rayTracingPipeline);
			vk::PhysicalDeviceRayQueryFeaturesKHR ray_query{ vk::True };
			AddFeature(ray_query, IsDeviceExtensionEnabled(vk::KHRRayQueryExtensionName), vk::StructureType::ePhysicalDeviceRayQueryFeaturesKHR, &ray_query.rayQuery);
			vk::PhysicalDeviceMeshShaderFeaturesEXT mesh_shader{ vk::True };
			AddFeature(mesh_shader, IsDeviceExtensionEnabled(vk::EXTMeshShaderExtensionName), vk::StructureType::ePhysicalDeviceMeshShaderFeaturesEXT, &mesh_shader.meshShader);
			vk::PhysicalDeviceMemoryPriorityFeaturesEXT memory_priority{ vk::True };
			AddFeature(memory_priority, IsDeviceExtensionEnabled(vk::EXTMemoryPriorityExtensionName), vk::StructureType::ePhysicalDeviceMemoryPriorityFeaturesEXT, &memory_priority.memoryPriority);
			vk::PhysicalDeviceShaderObjectFeaturesEXT shader_object{ vk::True };
			AddFeature(shader_object, IsDeviceExtensionEnabled(vk::EXTShaderObjectExtensionName), vk::StructureType::ePhysicalDeviceShaderObjectFeaturesEXT, &shader_object.shaderObject);

			vk::PhysicalDeviceFeatures2 supported_features{ {}, feature_chain };
			physical_device->impl->getFeatures2(&supported_features, *physical_device->dispatcher);

			std::unordered_set<vk::StructureType> enabled_features;
			// Every probed feature (core via the promoted structs, the rest via their
			// own structs) whose bool survived getFeatures2 is enabled.
			auto available_features = probed | std::views::filter(
				[](auto const& feature) {
					return *feature.second;
				}
			);
			std::ranges::transform(
				available_features,
				std::inserter(enabled_features, enabled_features.end()),
				[](auto const& feature) {
					return feature.first;
				}
			);

			// Build one VkDeviceQueueCreateInfo per queue family from the immutable
			// queue pool, preserving the priorities computed at QueueAllocator creation.
			auto queue_create_infos =
				queue_alloc.GetCreatePlans() |
				std::views::transform(
					[](auto const& plan) {
						return vk::DeviceQueueCreateInfo(
							vk::DeviceQueueCreateFlags{},
							plan.family,
							static_cast<std::uint32_t>(plan.priorities.size()),
							plan.priorities.data(),
							nullptr
						);
					}
				) |
				std::ranges::to<std::vector>();

			// Phase 4: create the logical device and initialize its dispatcher.
			vk::SharedDevice device(
				physical_device->impl->createDevice(
					{
						{},
						queue_create_infos,
						{},
						enabled_extensions,
						nullptr,      // pEnabledFeatures: base features ride the feature chain
						feature_chain // pNext: the validated feature chain
					}, 
					nullptr, 
					*physical_device->dispatcher
				),
				{ nullptr, *physical_device->dispatcher }
			);
			auto device_dispatcher = std::make_shared<vk::detail::DispatchLoaderDynamic>(*physical_device->dispatcher);
			device_dispatcher->init(*device);

			// Phase 5: wrap VMA around the device. The budget flag is only set when
			// VK_EXT_memory_budget was enabled; VMA consumes it for usage tracking.
			VmaVulkanFunctions vulkan_functions = {};
			vulkan_functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
			vulkan_functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
			VmaAllocatorCreateInfo allocator_info = {};
			allocator_info.flags = static_cast<VmaAllocatorCreateFlags>(
				IsDeviceExtensionEnabled(vk::EXTMemoryBudgetExtensionName) ?
					VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT : 0
			);
			allocator_info.vulkanApiVersion = device_version;
			allocator_info.physicalDevice = *physical_device->impl;
			allocator_info.device = *device;
			allocator_info.instance = *physical_device->impl.getDestructorType();
			allocator_info.pVulkanFunctions = &vulkan_functions;
			VmaAllocator allocator_impl = nullptr;
			if (vmaCreateAllocator(&allocator_info, &allocator_impl) != VK_SUCCESS || !allocator_impl) {
				throw std::runtime_error("Failed to create the VMA allocator");
			}
			vulkan::MemoryAllocator memory_allocator(device, allocator_impl);

			return MakeLogicalDevice(
				vulkan::LogicalDevice{
					physical_device->impl,
					std::move(queue_alloc),
					std::move(device),
					std::move(device_dispatcher),
					std::move(memory_allocator),
					std::move(enabled_features)
				}
			);
		}
	};

} // namespace fyuu_rhi
#endif // !defined(__APPLE__)
