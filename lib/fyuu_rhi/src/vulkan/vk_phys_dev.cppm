module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <vector>
#include <unordered_set>
#include <string>
#include <ranges>
#include <format>
#include <source_location>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#endif //!defined(__APPLE__)
#include "log.hpp"
module fyuu_rhi:vulkan_physical_device;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import vulkan;
import :core_types;
import :vulkan_traits;
import :vulkan_queue_allocator;
import :vulkan_utility;
import :log;

namespace {

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::vulkan;

	PhysicalDeviceInfo::Type GetPhysicalDeviceType(vk::PhysicalDeviceProperties const& props) {
		using Type = PhysicalDeviceInfo::Type;
		switch (props.deviceType) {
		case vk::PhysicalDeviceType::eDiscreteGpu: return Type::DiscreteGPU;
		case vk::PhysicalDeviceType::eIntegratedGpu: return Type::IntegratedGPU;
		case vk::PhysicalDeviceType::eCpu: return Type::CPU;
		case vk::PhysicalDeviceType::eVirtualGpu: return Type::Virtual;
		default: return Type::Unknown;
		}
	}

	struct OptionalDeviceExt {
		char const* device_ext_name;
		std::string_view required_instance_ext;
	};

	[[nodiscard]] bool IsInstanceExtensionEnabled(std::span<std::string const> enabled_instance_extensions, std::string_view ext_name) {
		if (ext_name.empty()) {
			return true;
		}
		for (auto enabled : enabled_instance_extensions) {
			if (enabled == ext_name) {
				return true;
			}
		}
		return false;
	}

	void ProcessOptionalDeviceExtensions(
		std::unordered_set<std::string_view> const& device_extensions,
		std::span<std::string const> enabled_instance_extensions,
		std::vector<char const*>& enabled_device_extensions,
		std::span<const OptionalDeviceExt> optional_exts
	) {
		for (auto const& ext : optional_exts) {
			if (!device_extensions.count(ext.device_ext_name)) {
				LOG_INFO(std::format("Extension {} is not supported by this device", ext.device_ext_name));
				continue;
			}
			if (!ext.required_instance_ext.empty() &&
				!IsInstanceExtensionEnabled(enabled_instance_extensions, ext.required_instance_ext)) {
				LOG_WARNING(
					std::format(
						"Extension {} is supported by this device but the instance extension {} is not enabled, skipped",
						ext.device_ext_name, ext.required_instance_ext
					)
				);
				continue;
			}
			enabled_device_extensions.emplace_back(ext.device_ext_name);
		}
	}

	void InsertSwapChainExtensions(
		std::unordered_set<std::string_view> const& device_extensions,
		std::span<std::string const> enabled_instance_extensions,
		std::vector<char const*>& enabled_device_extensions
	) {
		if (!device_extensions.count(vk::KHRSwapchainExtensionName)) {
			throw std::runtime_error(std::format("This device does not support {}", vk::KHRSwapchainExtensionName));
		}
		enabled_device_extensions.emplace_back(vk::KHRSwapchainExtensionName);

		static constexpr OptionalDeviceExt optional_exts[] = {
			{ vk::EXTSwapchainMaintenance1ExtensionName, vk::EXTSurfaceMaintenance1ExtensionName },
			{ vk::KHRMaintenance2ExtensionName, "" },
			{ vk::KHRImageFormatListExtensionName, "" },
			{ vk::KHRSwapchainMutableFormatExtensionName, "" },
			{ vk::KHRSurfaceProtectedCapabilitiesExtensionName, "" },
			{ vk::KHRPresentModeFifoLatestReadyExtensionName, vk::KHRGetSurfaceCapabilities2ExtensionName },
		};

		ProcessOptionalDeviceExtensions(
			device_extensions, 
			enabled_instance_extensions,
			enabled_device_extensions, 
			optional_exts
		);
	}

	[[nodiscard]] bool IsDeviceExtensionEnabled(
		std::span<char const* const> enabled_extensions,
		std::string_view extension
	) noexcept {
		for (auto enabled : enabled_extensions) {
			if (std::string_view(enabled) == extension) {
				return true;
			}
		}
		return false;
	}

	[[nodiscard]] bool EnablePromotedDeviceExtension(
		std::uint32_t api_version,
		std::uint32_t core_version,
		char const* extension_name,
		std::unordered_set<std::string_view> const& available_extensions,
		std::vector<char const*>& enabled_extensions
	) {
		if (api_version >= core_version) {
			return true;
		}
		if (!available_extensions.contains(extension_name)) {
			return false;
		}
		enabled_extensions.emplace_back(extension_name);
		return true;
	}

	std::vector<CommandQueueInfo> QueryQueueInfo(Backend::PhysicalDevice const& phys_dev) {

		auto&& [instance, phys_dev_impl] = phys_dev;

		std::vector<vk::QueueFamilyProperties> queue_families = phys_dev_impl->getQueueFamilyProperties(instance.dispatcher);
		std::vector<CommandQueueInfo> queue_infos;

		std::uint32_t length = static_cast<std::uint32_t>(queue_families.size());
		for (std::uint32_t i = 0; i < length; ++i) {
			vk::QueueFamilyProperties const& current_family = queue_families[i];

			CommandQueueType type = CommandQueueType::None;
			if (current_family.queueFlags & vk::QueueFlagBits::eGraphics) {
				type |= CommandQueueType::Graphics;
			}
			if (current_family.queueFlags & vk::QueueFlagBits::eCompute) {
				type |= CommandQueueType::Compute;
			}
			if (current_family.queueFlags & vk::QueueFlagBits::eTransfer) {
				type |= CommandQueueType::Copy;
			}

			if (type != CommandQueueType::None) {
				queue_infos.push_back({ type, i, current_family.queueCount });
			}
		}

		return queue_infos;

	}

}

namespace fyuu_rhi::vulkan {

	PhysicalDeviceInfo Backend::GetPhysicalDeviceInfo(Backend::PhysicalDevice const& phys_dev) {

		auto&& [instance, phys_dev_impl] = phys_dev;

		vk::PhysicalDeviceProperties props = phys_dev_impl->getProperties(instance.dispatcher);
		vk::PhysicalDeviceMemoryProperties mem_props = phys_dev_impl->getMemoryProperties(instance.dispatcher);

		std::size_t dedicated_memory = 0;
		for (std::uint32_t i = 0; i < mem_props.memoryHeapCount; ++i) {
			if (mem_props.memoryHeaps[i].flags & vk::MemoryHeapFlagBits::eDeviceLocal) {
				dedicated_memory += mem_props.memoryHeaps[i].size;
			}
		}

		PhysicalDeviceInfo info = {
			.name = props.deviceName,
			.vendor_id = props.vendorID,
			.device_id = props.deviceID,
			.dedicated_memory = dedicated_memory,
			.type = GetPhysicalDeviceType(props)
		};

		return info;
	}

	Backend::LogicalDevice Backend::CreateLogicalDevice(PhysicalDevice const& phys_dev) {

		std::vector<CommandQueueInfo> queue_infos = QueryQueueInfo(phys_dev);
		QueueAllocator queue_alloc(queue_infos);

		auto&& [instance, phys_dev_impl] = phys_dev;

		std::vector<vk::ExtensionProperties> extension_properties = phys_dev_impl->enumerateDeviceExtensionProperties();
		std::unordered_set<std::string_view> device_extensions;
		for (auto const& extension_property : extension_properties) {
			device_extensions.insert(extension_property.extensionName);
		}

		std::vector<char const*> enabled_extensions;

		InsertSwapChainExtensions(device_extensions, instance.enabled_extensions, enabled_extensions);
		auto properties = phys_dev_impl->getProperties(instance.dispatcher);
		bool timeline_semaphore_available = EnablePromotedDeviceExtension(
			properties.apiVersion,
			vk::ApiVersion12,
			vk::KHRTimelineSemaphoreExtensionName,
			device_extensions,
			enabled_extensions
		);
		bool dynamic_rendering_available = EnablePromotedDeviceExtension(
			properties.apiVersion,
			vk::ApiVersion13,
			vk::KHRDynamicRenderingExtensionName,
			device_extensions,
			enabled_extensions
		);
		bool synchronization2_available = EnablePromotedDeviceExtension(
			properties.apiVersion,
			vk::ApiVersion13,
			vk::KHRSynchronization2ExtensionName,
			device_extensions,
			enabled_extensions
		);

		vk::PhysicalDeviceTimelineSemaphoreFeatures timeline_semaphore_features;
		vk::PhysicalDeviceDynamicRenderingFeatures dynamic_rendering_features;
		vk::PhysicalDeviceSynchronization2Features synchronization2_features;
		vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR swapchain_maintenance_features;
		vk::PhysicalDevicePresentModeFifoLatestReadyFeaturesKHR fifo_latest_ready_features;
		void* feature_chain = nullptr;
		bool swapchain_maintenance_available = IsDeviceExtensionEnabled(
			enabled_extensions,
			vk::EXTSwapchainMaintenance1ExtensionName
		);
		bool fifo_latest_ready_available = IsDeviceExtensionEnabled(
			enabled_extensions,
			vk::KHRPresentModeFifoLatestReadyExtensionName
		);
		if (swapchain_maintenance_available) {
			swapchain_maintenance_features.pNext = feature_chain;
			feature_chain = &swapchain_maintenance_features;
		}
		if (fifo_latest_ready_available) {
			fifo_latest_ready_features.pNext = feature_chain;
			feature_chain = &fifo_latest_ready_features;
		}
		if (timeline_semaphore_available) {
			timeline_semaphore_features.pNext = feature_chain;
			feature_chain = &timeline_semaphore_features;
		}
		if (dynamic_rendering_available) {
			dynamic_rendering_features.pNext = feature_chain;
			feature_chain = &dynamic_rendering_features;
		}
		if (synchronization2_available) {
			synchronization2_features.pNext = feature_chain;
			feature_chain = &synchronization2_features;
		}

		vk::PhysicalDeviceFeatures2 supported_features({}, feature_chain);
		phys_dev_impl->getFeatures2(&supported_features, instance.dispatcher);
		bool timeline_semaphore_supported =
			timeline_semaphore_available &&
			timeline_semaphore_features.timelineSemaphore;
		timeline_semaphore_features.timelineSemaphore = timeline_semaphore_supported;
		bool dynamic_rendering_supported =
			dynamic_rendering_available &&
			dynamic_rendering_features.dynamicRendering;
		dynamic_rendering_features.dynamicRendering = dynamic_rendering_supported;
		bool synchronization2_supported =
			synchronization2_available &&
			synchronization2_features.synchronization2;
		synchronization2_features.synchronization2 = synchronization2_supported;
		bool swapchain_maintenance_supported =
			swapchain_maintenance_available && swapchain_maintenance_features.swapchainMaintenance1;
		swapchain_maintenance_features.swapchainMaintenance1 = swapchain_maintenance_supported;
		bool fifo_latest_ready_supported =
			fifo_latest_ready_available && fifo_latest_ready_features.presentModeFifoLatestReady;
		fifo_latest_ready_features.presentModeFifoLatestReady = fifo_latest_ready_supported;

		
		// --------------------------------------------------------------------
		// Queue creation.
		// We create one queue family per command type (graphics, compute, copy)
		// as determined by the queue alloc. The alloc provides the
		// family index, number of queues, and queue priorities.
		// --------------------------------------------------------------------
		std::vector<vk::DeviceQueueCreateInfo> queue_create_infos;

		std::unordered_set<std::uint32_t> queue_families;
		for (CommandQueueType type : {
			CommandQueueType::Graphics,
			CommandQueueType::Compute,
			CommandQueueType::Copy
		}) {
			if (!queue_alloc.Supports(type)) {
				continue;
			}

			auto family = queue_alloc.GetFamily(type);
			if (!queue_families.emplace(family).second) {
				continue;
			}

			queue_create_infos.emplace_back(
				vk::DeviceQueueCreateFlags{},
				family,
				queue_alloc.GetTotalQueue(type),
				queue_alloc.GetPriorities(type).data(),
				nullptr
			);
		}

		vk::DeviceCreateInfo device_create_info(
			{},					// flags_
			queue_create_infos, // queueCreateInfos_
			{},					// pEnabledLayerNames_
			enabled_extensions,	// pEnabledExtensionNames_
			nullptr,			// pEnabledFeatures_
			feature_chain
		);

		vk::SharedDevice dev(
			phys_dev_impl->createDevice(device_create_info, nullptr, instance.dispatcher),
			{ nullptr, instance.dispatcher }
		);

		auto dev_dispatcher = std::make_shared<vk::detail::DispatchLoaderDynamic>(instance.dispatcher);
		dev_dispatcher->init(*dev);

		// Provide VMA with the necessary function pointers.
		static VmaVulkanFunctions vulkan_functions = {};
		vulkan_functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
		vulkan_functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

		VmaAllocatorCreateInfo alloc_info = {};
		alloc_info.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;	// Enable memory budget extension.
		alloc_info.vulkanApiVersion = properties.apiVersion;
		alloc_info.physicalDevice = *phys_dev_impl;						// Physical device.
		alloc_info.device = *dev;										// Logical device.
		alloc_info.instance = *instance.impl;							// Vulkan instance.
		alloc_info.pVulkanFunctions = &vulkan_functions;				// Function pointers.

		VmaAllocator mem_alloc_impl;
		vmaCreateAllocator(&alloc_info, &mem_alloc_impl);
		if (!mem_alloc_impl) {
			throw std::runtime_error("CreateLogicalDevice(): Failed to create VMA allocator");
		}

		auto mem_alloc = std::make_shared<VMAAllocator>(dev, mem_alloc_impl);
		std::unordered_set<vk::StructureType> enabled_features;
		if (timeline_semaphore_supported) {
			enabled_features.emplace(vk::StructureType::ePhysicalDeviceTimelineSemaphoreFeatures);
		}
		if (dynamic_rendering_supported) {
			enabled_features.emplace(vk::StructureType::ePhysicalDeviceDynamicRenderingFeatures);
		}
		if (synchronization2_supported) {
			enabled_features.emplace(vk::StructureType::ePhysicalDeviceSynchronization2Features);
		}
		if (swapchain_maintenance_supported) {
			enabled_features.emplace(
				vk::StructureType::ePhysicalDeviceSwapchainMaintenance1FeaturesKHR
			);
		}
		if (fifo_latest_ready_supported) {
			enabled_features.emplace(
				vk::StructureType::ePhysicalDevicePresentModeFifoLatestReadyFeaturesKHR
			);
		}

		return {
			phys_dev,
			std::move(queue_alloc),
			ToStrings(enabled_extensions),
			std::move(enabled_features),
			dev,
			dev_dispatcher,
			mem_alloc,
			std::make_shared<LogicalDevice::PresentationCache>(),
			execution::CompletionService::Instance()
		};
	}

}
#endif // !defined(__APPLE__)
