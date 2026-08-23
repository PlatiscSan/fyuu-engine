module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <stdexcept>

#include <limits>

#include <memory>

#include <deque>
#include <vector>

#include <functional>

#include <cstdint>
#include <utility>

#include <mutex>

#include <variant>

#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#if defined(_WIN32)
#include <Windows.h>
#endif // defined(_WIN32)
#if defined(__clang__) && defined(_MSVC_STL_VERSION)
#define FYUU_RHI_USE_VULKAN_HEADER
#include <vulkan/vulkan_shared.hpp>
#endif
#endif // !defined(__APPLE__)

module fyuu_rhi:vulkan_command_scheduler;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
#if !defined(FYUU_RHI_USE_VULKAN_HEADER)
import vulkan;
#endif
import :command_scheduler_dispatch;
import :command_scheduler_factory;
import :completion_token_factory;
import :execution;
import :logical_device_dispatch;
import :pipeline_factory;
import :pipeline_resource_group_factory;
import :resource_factory;
import :sampler_factory;
import :view_factory;
import :vulkan_data;
import :vulkan_queue_allocator;
import :vulkan_utility;

namespace fyuu_rhi::details {
	extern void ParallelFor(
		std::size_t first,
		std::size_t last,
		void* function,
		void (*invoke)(void*, std::size_t)
	);
}

namespace {

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::execution;
	using namespace fyuu_rhi::vulkan;
	using fyuu_rhi::vulkan::CommandQueueType;

	template <class Function>
	void ParallelFor(
		std::size_t first,
		std::size_t last,
		Function&& function
	) {
		fyuu_rhi::details::ParallelFor(
			first,
			last,
			std::addressof(function),
			[](void* erased_function, std::size_t index) {
				(*static_cast<std::remove_reference_t<Function>*>(erased_function))(
					index
				);
			}
		);
	}

	CommandQueueType NativeCapabilities(QueueType type) {
		switch (type) {
		case QueueType::Graphics:
			return CommandQueueType::Graphics;
		case QueueType::Compute:
			return CommandQueueType::Compute;
		case QueueType::Transfer:
			return CommandQueueType::Copy;
		case QueueType::Present:
			return CommandQueueType::Graphics;
		}
		throw std::invalid_argument("Unknown Vulkan queue type");
	}

	std::uint64_t EstimateWork(ExecutionBatch const& batch) noexcept {
		std::uint64_t result = 1u;
		for (auto const& node : batch.nodes) {
			result += static_cast<std::uint64_t>(node.commands.size());
			result += static_cast<std::uint64_t>(node.accesses.size());
		}
		return result;
	}

	vulkan::Resource& NativeBuffer(vulkan::Resource& resource) {
		if (!resource.allocation.GetBuffer()) {
			throw std::invalid_argument("Vulkan command requires a buffer resource");
		}
		return resource;
	}

	vulkan::Resource& NativeTexture(vulkan::Resource& resource) {
		if (!resource.allocation.GetImage()) {
			throw std::invalid_argument("Vulkan command requires a texture resource");
		}
		return resource;
	}

	vk::SharedImageView const& NativeImageView(vulkan::View const& view) {
		auto result = std::get_if<vk::SharedImageView>(&view.impl);
		if (!result) {
			throw std::invalid_argument("Vulkan rendering requires an image view");
		}
		return *result;
	}

	vk::ImageAspectFlags NativeAspect(vk::Format format) noexcept {
		switch (format) {
		case vk::Format::eD16Unorm:
		case vk::Format::eD32Sfloat:
			return vk::ImageAspectFlagBits::eDepth;
		case vk::Format::eS8Uint:
			return vk::ImageAspectFlagBits::eStencil;
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32SfloatS8Uint:
			return vk::ImageAspectFlagBits::eDepth |
				vk::ImageAspectFlagBits::eStencil;
		default:
			return vk::ImageAspectFlagBits::eColor;
		}
	}

	std::uint32_t TexelSize(vk::Format format) {
		switch (format) {
		case vk::Format::eR8Unorm:
		case vk::Format::eR8Snorm:
		case vk::Format::eR8Uint:
		case vk::Format::eR8Sint:
			return 1u;
		case vk::Format::eR8G8Unorm:
		case vk::Format::eR8G8Snorm:
		case vk::Format::eR8G8Uint:
		case vk::Format::eR8G8Sint:
		case vk::Format::eR16Unorm:
		case vk::Format::eR16Snorm:
		case vk::Format::eR16Uint:
		case vk::Format::eR16Sint:
		case vk::Format::eR16Sfloat:
		case vk::Format::eD16Unorm:
			return 2u;
		case vk::Format::eR8G8B8A8Unorm:
		case vk::Format::eR8G8B8A8Snorm:
		case vk::Format::eR8G8B8A8Uint:
		case vk::Format::eR8G8B8A8Sint:
		case vk::Format::eR8G8B8A8Srgb:
		case vk::Format::eB8G8R8A8Srgb:
		case vk::Format::eR16G16Unorm:
		case vk::Format::eR16G16Snorm:
		case vk::Format::eR16G16Uint:
		case vk::Format::eR16G16Sint:
		case vk::Format::eR16G16Sfloat:
		case vk::Format::eR32Uint:
		case vk::Format::eR32Sint:
		case vk::Format::eR32Sfloat:
		case vk::Format::eA2R10G10B10UnormPack32:
		case vk::Format::eA2R10G10B10UintPack32:
		case vk::Format::eB10G11R11UfloatPack32:
		case vk::Format::eE5B9G9R9UfloatPack32:
		case vk::Format::eD32Sfloat:
		case vk::Format::eD24UnormS8Uint:
			return 4u;
		case vk::Format::eR16G16B16A16Unorm:
		case vk::Format::eR16G16B16A16Snorm:
		case vk::Format::eR16G16B16A16Uint:
		case vk::Format::eR16G16B16A16Sint:
		case vk::Format::eR16G16B16A16Sfloat:
		case vk::Format::eR32G32Uint:
		case vk::Format::eR32G32Sint:
		case vk::Format::eR32G32Sfloat:
		case vk::Format::eD32SfloatS8Uint:
			return 8u;
		case vk::Format::eR32G32B32A32Uint:
		case vk::Format::eR32G32B32A32Sint:
		case vk::Format::eR32G32B32A32Sfloat:
			return 16u;
		default:
			throw std::invalid_argument("Vulkan padded buffer copy does not support this format");
		}
	}

	struct NativeAccess {
		/// synchronization2 is the canonical representation. The legacy path
		/// narrows these masks only at the vkCmdPipelineBarrier boundary.
		vk::PipelineStageFlags2 stages;
		vk::AccessFlags2 access;
		vk::ImageLayout layout;
	};

	vk::AccessFlags LegacyAccess(vk::AccessFlags2 value) noexcept {
		return vk::AccessFlags(
			static_cast<vk::AccessFlags::MaskType>(
				static_cast<vk::AccessFlags2::MaskType>(value)
			)
		);
	}

	vk::PipelineStageFlags LegacyStages(vk::PipelineStageFlags2 value) noexcept {
		return vk::PipelineStageFlags(
			static_cast<vk::PipelineStageFlags::MaskType>(
				static_cast<vk::PipelineStageFlags2::MaskType>(value)
			)
		);
	}

	NativeAccess MapAccess(ResourceUsage usage, AccessMode mode) noexcept {
		if (HasUsage(usage, ResourceUsage::ColorAttachment)) {
			return {
				vk::PipelineStageFlagBits2::eColorAttachmentOutput,
				mode == AccessMode::Read ?
					vk::AccessFlagBits2::eColorAttachmentRead :
					vk::AccessFlagBits2::eColorAttachmentRead |
						vk::AccessFlagBits2::eColorAttachmentWrite,
				vk::ImageLayout::eColorAttachmentOptimal
			};
		}
		if (HasUsage(usage, ResourceUsage::DepthStencilAttachment)) {
			return {
				vk::PipelineStageFlagBits2::eEarlyFragmentTests |
					vk::PipelineStageFlagBits2::eLateFragmentTests,
				mode == AccessMode::Read ?
					vk::AccessFlagBits2::eDepthStencilAttachmentRead :
					vk::AccessFlagBits2::eDepthStencilAttachmentRead |
						vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
				mode == AccessMode::Read ?
					vk::ImageLayout::eDepthStencilReadOnlyOptimal :
					vk::ImageLayout::eDepthStencilAttachmentOptimal
			};
		}
		if (HasUsage(usage, ResourceUsage::CopyDestination) ||
			HasUsage(usage, ResourceUsage::ResolveDestination)) {
			return {
				vk::PipelineStageFlagBits2::eTransfer,
				vk::AccessFlagBits2::eTransferWrite,
				vk::ImageLayout::eTransferDstOptimal
			};
		}
		if (HasUsage(usage, ResourceUsage::CopySource) ||
			HasUsage(usage, ResourceUsage::ResolveSource) ||
			HasUsage(usage, ResourceUsage::PresentationSource)) {
			return {
				vk::PipelineStageFlagBits2::eTransfer,
				vk::AccessFlagBits2::eTransferRead,
				vk::ImageLayout::eTransferSrcOptimal
			};
		}
		if (HasUsage(usage, ResourceUsage::Storage)) {
			return {
				vk::PipelineStageFlagBits2::eAllCommands,
				mode == AccessMode::Read ?
					vk::AccessFlagBits2::eShaderStorageRead :
					mode == AccessMode::Write ?
						vk::AccessFlagBits2::eShaderStorageWrite :
						vk::AccessFlagBits2::eShaderStorageRead |
							vk::AccessFlagBits2::eShaderStorageWrite,
				vk::ImageLayout::eGeneral
			};
		}
		if (HasUsage(usage, ResourceUsage::Sampled)) {
			return {
				vk::PipelineStageFlagBits2::eAllCommands,
				vk::AccessFlagBits2::eShaderSampledRead,
				vk::ImageLayout::eShaderReadOnlyOptimal
			};
		}
		vk::AccessFlags2 access;
		vk::PipelineStageFlags2 stages = vk::PipelineStageFlagBits2::eAllCommands;
		if (HasUsage(usage, ResourceUsage::Indirect)) {
			access |= vk::AccessFlagBits2::eIndirectCommandRead;
			stages = vk::PipelineStageFlagBits2::eDrawIndirect;
		}
		if (HasUsage(usage, ResourceUsage::VertexBuffer)) {
			access |= vk::AccessFlagBits2::eVertexAttributeRead;
			stages = vk::PipelineStageFlagBits2::eVertexInput;
		}
		if (HasUsage(usage, ResourceUsage::IndexBuffer)) {
			access |= vk::AccessFlagBits2::eIndexRead;
			stages = vk::PipelineStageFlagBits2::eVertexInput;
		}
		if (HasUsage(usage, ResourceUsage::Uniform)) {
			access |= vk::AccessFlagBits2::eUniformRead;
		}
		return { stages, access, vk::ImageLayout::eGeneral };
	}

	struct ResourceMetadata {
		std::variant<std::size_t, ResourceTextureExtent> size_or_extent;
		vk::Format format;
		vk::ImageType image_type;
		vk::SampleCountFlagBits samples;
	};

	ResourceTextureExtent const& TextureExtent(ResourceMetadata const& metadata) {
		return std::get<ResourceTextureExtent>(metadata.size_or_extent);
	}

	vk::ImageSubresourceRange NativeRange(
		ResourceMetadata const& texture,
		ResourceRange const& range
	) {
		auto format = texture.format;
		if (auto concrete = std::get_if<TextureRange>(&range)) {
			return vk::ImageSubresourceRange(
				NativeAspect(format),
				concrete->base_mip_level,
				concrete->mip_level_count,
				concrete->base_array_layer,
				concrete->array_layer_count
			);
		}
		return vk::ImageSubresourceRange(
			NativeAspect(format),
			0u,
			TextureExtent(texture).mip_levels,
			0u,
			texture.image_type == vk::ImageType::e3D ?
				1u :
				TextureExtent(texture).depth_or_array_layers
		);
	}

	void EmitBarrier(
		vk::CommandBuffer commands,
		vulkan::Resource& resource,
		ResourceMetadata const& metadata,
		NativeAccess source,
		NativeAccess destination,
		ResourceRange const& range,
		std::uint32_t source_family,
		std::uint32_t destination_family,
		bool synchronization2,
		vk::detail::DispatchLoaderDynamic const& dispatcher
	) {
		// Queue-family indices are either both ignored for an ordinary dependency,
		// or form the matching release/acquire ownership-transfer pair emitted in
		// the source and destination command buffers respectively.
		if (std::holds_alternative<ResourceTextureExtent>(metadata.size_or_extent)) {
			vk::ImageMemoryBarrier2 barrier;
			barrier.srcStageMask = source.stages;
			barrier.srcAccessMask = source.access;
			barrier.dstStageMask = destination.stages;
			barrier.dstAccessMask = destination.access;
			barrier.oldLayout = source.layout;
			barrier.newLayout = destination.layout;
			barrier.srcQueueFamilyIndex = source_family;
			barrier.dstQueueFamilyIndex = destination_family;
			barrier.image = resource.allocation.GetImage();
			barrier.subresourceRange = NativeRange(metadata, range);
			if (synchronization2) {
				vk::DependencyInfo dependency;
				dependency.imageMemoryBarrierCount = 1u;
				dependency.pImageMemoryBarriers = &barrier;
				commands.pipelineBarrier2(dependency, dispatcher);
			}
			else {
				vk::ImageMemoryBarrier legacy;
				legacy.srcAccessMask = LegacyAccess(source.access);
				legacy.dstAccessMask = LegacyAccess(destination.access);
				legacy.oldLayout = source.layout;
				legacy.newLayout = destination.layout;
				legacy.srcQueueFamilyIndex = source_family;
				legacy.dstQueueFamilyIndex = destination_family;
				legacy.image = resource.allocation.GetImage();
				legacy.subresourceRange = barrier.subresourceRange;
				commands.pipelineBarrier(
					LegacyStages(source.stages),
					LegacyStages(destination.stages),
					vk::DependencyFlags{},
					0u,
					nullptr,
					0u,
					nullptr,
					1u,
					&legacy,
					dispatcher
				);
			}
			return;
		}
		auto& buffer = NativeBuffer(resource);
		vk::BufferMemoryBarrier2 barrier;
		barrier.srcStageMask = source.stages;
		barrier.srcAccessMask = source.access;
		barrier.dstStageMask = destination.stages;
		barrier.dstAccessMask = destination.access;
		barrier.srcQueueFamilyIndex = source_family;
		barrier.dstQueueFamilyIndex = destination_family;
		barrier.buffer = buffer.allocation.GetBuffer();
		barrier.offset = 0u;
		barrier.size = (std::numeric_limits<vk::DeviceSize>::max)();
		if (auto concrete = std::get_if<BufferRange>(&range)) {
			barrier.offset = concrete->offset;
			barrier.size = concrete->size;
		}
		if (synchronization2) {
			vk::DependencyInfo dependency;
			dependency.bufferMemoryBarrierCount = 1u;
			dependency.pBufferMemoryBarriers = &barrier;
			commands.pipelineBarrier2(dependency, dispatcher);
		}
		else {
			vk::BufferMemoryBarrier legacy;
			legacy.srcAccessMask = LegacyAccess(source.access);
			legacy.dstAccessMask = LegacyAccess(destination.access);
			legacy.srcQueueFamilyIndex = source_family;
			legacy.dstQueueFamilyIndex = destination_family;
			legacy.buffer = buffer.allocation.GetBuffer();
			legacy.offset = barrier.offset;
			legacy.size = barrier.size;
			commands.pipelineBarrier(
				LegacyStages(source.stages),
				LegacyStages(destination.stages),
				vk::DependencyFlags{},
				0u,
				nullptr,
				1u,
				&legacy,
				0u,
				nullptr,
				dispatcher
			);
		}
	}

	struct PresentationWork {
		std::size_t batch;
		std::size_t source;
		PlatformHandle target;
		std::uint32_t family;
		vk::SharedSwapchainKHR swapchain;
		vk::Image back_buffer;
		/// Swapchain (surface) extent; the source is blit-scaled to it when different.
		vk::Extent2D extent;
		vk::SharedSemaphore image_available;
		vk::SharedSemaphore present_ready;
		vk::SharedFence retirement_fence;
		std::uint32_t image_index;
		bool swapchain_maintenance1_supported;
	};

	vk::SharedSurfaceKHR CreateSurface(
		vk::SharedPhysicalDevice const& physical_device,
		PlatformHandle target,
		vk::detail::DispatchLoaderDynamic const& dispatcher
	) {
		auto const& instance = physical_device.getDestructorType();
#if defined(_WIN32)
		if (!target) {
			throw std::invalid_argument("Vulkan presentation target is null");
		}
		vk::Win32SurfaceCreateInfoKHR info(
			vk::Win32SurfaceCreateFlagsKHR{},
			GetModuleHandleW(nullptr),
			target
		);
		auto native = instance->createWin32SurfaceKHR(info, nullptr, dispatcher);
#elif defined(__ANDROID__)
		if (!target) {
			throw std::invalid_argument("Vulkan presentation target is null");
		}
		vk::AndroidSurfaceCreateInfoKHR info(
			vk::AndroidSurfaceCreateFlagsKHR{},
			target
		);
		auto native = instance->createAndroidSurfaceKHR(info, nullptr, dispatcher);
#elif defined(__linux__)
		vk::SurfaceKHR native = std::visit(
			[&](auto const& handle) -> vk::SurfaceKHR {
				using Handle = std::remove_cvref_t<decltype(handle)>;
				if constexpr (std::same_as<Handle, X11PlatformHandle>) {
					vk::XlibSurfaceCreateInfoKHR info(
						vk::XlibSurfaceCreateFlagsKHR{},
						handle.display,
						handle.window
					);
					return instance->createXlibSurfaceKHR(info, nullptr, dispatcher);
				}
				else {
					vk::WaylandSurfaceCreateInfoKHR info(
						vk::WaylandSurfaceCreateFlagsKHR{},
						handle.display,
						handle.surface
					);
					return instance->createWaylandSurfaceKHR(info, nullptr, dispatcher);
				}
			},
			target
		);
#endif
		return vk::SharedSurfaceKHR(
			native,
			instance,
			{ nullptr, dispatcher }
		);
	}

	std::pair<vk::PresentModeKHR, bool> SelectPresentMode(
		vulkan::CommandSchedulerContext const* scheduler,
		vk::SharedSurfaceKHR const& surface,
		bool vertical_sync
	) {
		auto modes = scheduler->physical_device->getSurfacePresentModesKHR(
			*surface,
			*scheduler->dispatcher
		);
		vk::PhysicalDevicePresentModeFifoLatestReadyFeaturesKHR fifo_features;
		vk::PhysicalDeviceFeatures2 device_features;
		device_features.pNext = &fifo_features;
		scheduler->physical_device->getFeatures2(
			&device_features,
			*scheduler->dispatcher
		);
		auto fifo_latest_ready_supported =
			fifo_features.presentModeFifoLatestReady &&
			std::ranges::find(modes, vk::PresentModeKHR::eFifoLatestReady) !=
			modes.end();
		if (vertical_sync) {
			// Mailbox is synchronized to vertical refresh and keeps only the newest
			// queued frame, so prefer it over either FIFO mode when it is available.
			if (std::ranges::find(modes, vk::PresentModeKHR::eMailbox) != modes.end()) {
				return { vk::PresentModeKHR::eMailbox, fifo_latest_ready_supported };
			}
			return {
				fifo_latest_ready_supported ?
					vk::PresentModeKHR::eFifoLatestReady :
					vk::PresentModeKHR::eFifo,
				fifo_latest_ready_supported
			};
		}
		if (std::ranges::find(modes, vk::PresentModeKHR::eImmediate) != modes.end()) {
			return { vk::PresentModeKHR::eImmediate, fifo_latest_ready_supported };
		}
		if (std::ranges::find(modes, vk::PresentModeKHR::eMailbox) != modes.end()) {
			return { vk::PresentModeKHR::eMailbox, fifo_latest_ready_supported };
		}
		if (fifo_latest_ready_supported) {
			return { vk::PresentModeKHR::eFifoLatestReady, true };
		}
		return { vk::PresentModeKHR::eFifo, fifo_latest_ready_supported };
	}

	/// The swapchain follows the surface extent, not the presentation source:
	/// Vulkan fixes currentExtent on most window systems and offers no stretch
	/// scaling, so the source is blit-scaled into the back buffer at present
	/// time whenever the two differ (mirroring D3D12's DXGI_SCALING_STRETCH).
	vk::Extent2D ResolveSwapchainExtent(
		vk::SurfaceCapabilitiesKHR const& capabilities,
		ResourceMetadata const& source
	) noexcept {
		if (capabilities.currentExtent.width != (std::numeric_limits<std::uint32_t>::max)()) {
			return capabilities.currentExtent;
		}
		return vk::Extent2D(
			std::clamp(
				TextureExtent(source).width,
				capabilities.minImageExtent.width,
				capabilities.maxImageExtent.width
			),
			std::clamp(
				TextureExtent(source).height,
				capabilities.minImageExtent.height,
				capabilities.maxImageExtent.height
			)
		);
	}

	PresentationContext::SwapChain CreateSwapChain(
		vulkan::CommandSchedulerContext const* scheduler,
		vk::SharedSurfaceKHR const& surface,
		ResourceMetadata const& source,
		std::uint32_t family,
		std::uint32_t buffer_count,
		vk::PresentModeKHR present_mode,
		bool fifo_latest_ready_supported,
		PresentationContext::SwapChain const* previous
	) {
		if (source.image_type != vk::ImageType::e2D ||
			source.samples != vk::SampleCountFlagBits::e1 ||
			TextureExtent(source).mip_levels != 1u ||
			TextureExtent(source).depth_or_array_layers != 1u) {
			throw std::invalid_argument(
				"Vulkan presentation source must be a single-sampled 2D texture"
			);
		}
		auto capabilities = scheduler->physical_device->getSurfaceCapabilitiesKHR(
			*surface,
			*scheduler->dispatcher
		);
		if (buffer_count < capabilities.minImageCount ||
			(capabilities.maxImageCount != 0u && buffer_count > capabilities.maxImageCount)) {
			throw std::invalid_argument("Vulkan swapchain buffer count is unsupported");
		}
		auto formats = scheduler->physical_device->getSurfaceFormatsKHR(
			*surface,
			*scheduler->dispatcher
		);
		auto source_format = static_cast<vk::Format>(source.format);
		auto format = std::ranges::find_if(
			formats,
			[source_format](vk::SurfaceFormatKHR const& candidate) {
				return candidate.format == source_format;
			}
		);
		if (format == formats.end()) {
			if (formats.size() == 1u && formats.front().format == vk::Format::eUndefined) {
				format = formats.begin();
				format->format = source_format;
			}
			else {
				throw std::invalid_argument("Vulkan presentation source format is unsupported");
			}
		}
		vk::Extent2D extent = ResolveSwapchainExtent(capabilities, source);
		if ((capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst) ==
			vk::ImageUsageFlags{}) {
			throw std::runtime_error("Vulkan surface images do not support transfer destinations");
		}
		vk::CompositeAlphaFlagBitsKHR composite_alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
		if ((capabilities.supportedCompositeAlpha & composite_alpha) ==
			vk::CompositeAlphaFlagsKHR{}) {
			constexpr std::array alternatives{
				vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
				vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
				vk::CompositeAlphaFlagBitsKHR::eInherit
			};
			for (auto candidate : alternatives) {
				if ((capabilities.supportedCompositeAlpha & candidate) !=
					vk::CompositeAlphaFlagsKHR{}) {
					composite_alpha = candidate;
					break;
				}
			}
		}
		vk::SwapchainCreateInfoKHR info(
			vk::SwapchainCreateFlagsKHR{},
			*surface,
			buffer_count,
			format->format,
			format->colorSpace,
			extent,
			1u,
			vk::ImageUsageFlagBits::eTransferDst,
			vk::SharingMode::eExclusive,
			{},
			capabilities.currentTransform,
			composite_alpha,
			present_mode,
			true
		);
		// Vulkan resizes a swapchain by creating its replacement. Supplying the
		// previous handle lets the implementation transfer reusable presentation
		// resources, while the old object remains valid if creation fails.
		if (previous) {
			info.oldSwapchain = *previous->impl;
		}
		auto native = scheduler->device->createSwapchainKHR(
			info,
			nullptr,
			*scheduler->dispatcher
		);
		PresentationContext::SwapChain result;
		result.surface = surface;
		result.impl = vk::SharedSwapchainKHR(
			native,
			scheduler->device,
			surface,
			{ nullptr, *scheduler->dispatcher }
		);
		result.back_buffers = scheduler->device->getSwapchainImagesKHR(
			native,
			*scheduler->dispatcher
		);
		result.synchronization.reserve(result.back_buffers.size());
		for (std::size_t index = 0u; index < result.back_buffers.size(); ++index) {
			auto semaphore = scheduler->device->createSemaphore(
				vk::SemaphoreCreateInfo{},
				nullptr,
				*scheduler->dispatcher
			);
			auto fence = scheduler->device->createFence(
				vk::FenceCreateInfo(vk::FenceCreateFlagBits::eSignaled),
				nullptr,
				*scheduler->dispatcher
			);
			result.synchronization.emplace_back(
				vk::SharedSemaphore(
					semaphore,
					scheduler->device,
					{ nullptr, *scheduler->dispatcher }
				),
				vk::SharedFence(
					fence,
					scheduler->device,
					{ nullptr, *scheduler->dispatcher }
				),
				0u
			);
		}
		result.current_back_buffer_index = 0u;
		result.last_back_buffer_index = 0u;
		result.next_presentation_id = 1u;
		result.present_family = family;
		result.format = format->format;
		result.extent = extent;
		result.buffer_count = buffer_count;
		result.present_mode = present_mode;
		result.out_of_date = false;
		vk::PhysicalDeviceSwapchainMaintenance1FeaturesKHR maintenance_features;
		vk::PhysicalDeviceFeatures2 device_features;
		device_features.pNext = &maintenance_features;
		scheduler->physical_device->getFeatures2(
			&device_features,
			*scheduler->dispatcher
		);
		result.swapchain_maintenance1_supported = maintenance_features.swapchainMaintenance1;
		result.fifo_latest_ready_supported = fifo_latest_ready_supported;
		result.present_id_supported = false;
		return result;
	}

	struct Recorder {
		std::span<std::reference_wrapper<vulkan::Resource> const> resources;
		std::span<ResourceMetadata const> resource_metadata;
		std::span<std::reference_wrapper<vulkan::View> const> views;
		std::span<std::reference_wrapper<vulkan::Pipeline> const> pipelines;
		std::span<std::reference_wrapper<vulkan::PipelineResourceGroup> const> groups;
		vk::CommandBuffer commands;
		vk::detail::DispatchLoaderDynamic const& dispatcher;
		vk::SharedDevice const& device;
		bool dynamic_rendering_supported;
		bool synchronization2_supported;
		std::vector<vk::SharedRenderPass>* retained_render_passes;
		std::vector<vk::SharedFramebuffer>* retained_framebuffers;
		std::vector<PresentationWork>* presentation_works;
		std::vector<std::size_t> const* presentation_indices;
		ExecutionNode const* node = nullptr;
		std::size_t presentation_cursor = 0u;
		vulkan::Pipeline const* pipeline = nullptr;
		std::vector<vk::Format> rendering_color_formats;
		vk::Format rendering_depth_stencil_format = vk::Format::eUndefined;
		vk::SampleCountFlagBits rendering_samples = vk::SampleCountFlagBits::e1;
		bool rendering_samples_set = false;
		bool rendering = false;
		bool legacy_rendering = false;

		vk::ImageLayout DepthStencilLayout(std::size_t resource) const {
			if (node) {
				auto access = std::ranges::find_if(
					node->accesses,
					[resource](ResourceAccess const& candidate) {
						return candidate.resource == resource &&
							HasUsage(candidate.usage, ResourceUsage::DepthStencilAttachment);
					}
				);
				if (access != node->accesses.end() && access->mode == AccessMode::Read) {
					return vk::ImageLayout::eDepthStencilReadOnlyOptimal;
				}
			}
			return vk::ImageLayout::eDepthStencilAttachmentOptimal;
		}

		void operator()(BeginRendering const& value) {
			if (rendering) {
				throw std::logic_error("Nested Vulkan BeginRendering is invalid");
			}
			if (!dynamic_rendering_supported) {
				rendering_color_formats.clear();
				rendering_depth_stencil_format = vk::Format::eUndefined;
				rendering_samples_set = false;
				std::vector<vk::AttachmentDescription> attachments;
				std::vector<vk::AttachmentReference> color_references;
				std::vector<vk::AttachmentReference> resolve_references;
				std::vector<vk::ImageView> framebuffer_views;
				std::vector<vk::ClearValue> clear_values;
				attachments.reserve(
					value.colors.size() * 2u + (value.depth_stencil ? 1u : 0u)
				);
				color_references.reserve(value.colors.size());
				resolve_references.reserve(value.colors.size());
				bool has_resolve = false;
				std::optional<vk::Extent2D> framebuffer_extent;
				auto include_attachment_extent = [&](ResourceMetadata const& texture,
					vulkan::View const& view) {
					auto mip = view.subresource_range.baseMipLevel;
					vk::Extent2D extent(
						(std::max)(1u, TextureExtent(texture).width >> mip),
						(std::max)(1u, TextureExtent(texture).height >> mip)
					);
					if (framebuffer_extent && *framebuffer_extent != extent) {
						throw std::invalid_argument(
							"Vulkan framebuffer attachments have different extents"
						);
					}
					framebuffer_extent = extent;
				};
				for (auto const& color : value.colors) {
					auto const& view = views[color.view].get();
					auto const& texture = resource_metadata[color.resource];
					include_attachment_extent(texture, view);
					rendering_color_formats.emplace_back(view.format);
					auto samples = static_cast<vk::SampleCountFlagBits>(texture.samples);
					if (rendering_samples_set && rendering_samples != samples) {
						throw std::invalid_argument(
							"Vulkan rendering attachments have different sample counts"
						);
					}
					rendering_samples = samples;
					rendering_samples_set = true;
					auto attachment = static_cast<std::uint32_t>(attachments.size());
					attachments.emplace_back(
						vk::AttachmentDescriptionFlags{},
						view.format,
						static_cast<vk::SampleCountFlagBits>(texture.samples),
						color.load == LoadOperation::Load ?
							vk::AttachmentLoadOp::eLoad :
							color.load == LoadOperation::Clear ?
								vk::AttachmentLoadOp::eClear :
								vk::AttachmentLoadOp::eDontCare,
						color.store == StoreOperation::Store ?
							vk::AttachmentStoreOp::eStore :
							vk::AttachmentStoreOp::eDontCare,
						vk::AttachmentLoadOp::eDontCare,
						vk::AttachmentStoreOp::eDontCare,
						vk::ImageLayout::eColorAttachmentOptimal,
						vk::ImageLayout::eColorAttachmentOptimal
					);
					color_references.emplace_back(
						attachment,
						vk::ImageLayout::eColorAttachmentOptimal
					);
					framebuffer_views.emplace_back(*NativeImageView(view));
					clear_values.emplace_back(
						vk::ClearColorValue(
							std::array{
								color.clear.red,
								color.clear.green,
								color.clear.blue,
								color.clear.alpha
							}
						)
					);
					if (color.resolve_view) {
						auto const& resolve_view = views[*color.resolve_view].get();
							auto const& resolve_texture = resource_metadata[*color.resolve_resource];
						include_attachment_extent(resolve_texture, resolve_view);
						auto resolve_attachment = static_cast<std::uint32_t>(
							attachments.size()
						);
						attachments.emplace_back(
							vk::AttachmentDescriptionFlags{},
							resolve_view.format,
							vk::SampleCountFlagBits::e1,
							vk::AttachmentLoadOp::eDontCare,
							vk::AttachmentStoreOp::eStore,
							vk::AttachmentLoadOp::eDontCare,
							vk::AttachmentStoreOp::eDontCare,
							vk::ImageLayout::eColorAttachmentOptimal,
							vk::ImageLayout::eColorAttachmentOptimal
						);
						resolve_references.emplace_back(
							resolve_attachment,
							vk::ImageLayout::eColorAttachmentOptimal
						);
						framebuffer_views.emplace_back(*NativeImageView(resolve_view));
						clear_values.emplace_back(vk::ClearValue{});
						has_resolve = true;
					}
					else {
						resolve_references.emplace_back(
							(std::numeric_limits<std::uint32_t>::max)(),
							vk::ImageLayout::eUndefined
						);
					}
				}
				std::optional<vk::AttachmentReference> depth_reference;
				if (value.depth_stencil) {
					auto depth_layout = DepthStencilLayout(
						value.depth_stencil->resource
					);
					auto const& view = views[value.depth_stencil->view].get();
					auto const& texture = resource_metadata[value.depth_stencil->resource];
					include_attachment_extent(texture, view);
					rendering_depth_stencil_format = view.format;
					auto samples = static_cast<vk::SampleCountFlagBits>(texture.samples);
					if (rendering_samples_set && rendering_samples != samples) {
						throw std::invalid_argument(
							"Vulkan rendering attachments have different sample counts"
						);
					}
					rendering_samples = samples;
					rendering_samples_set = true;
					auto attachment = static_cast<std::uint32_t>(attachments.size());
					attachments.emplace_back(
						vk::AttachmentDescriptionFlags{},
						view.format,
						static_cast<vk::SampleCountFlagBits>(texture.samples),
						value.depth_stencil->depth_load == LoadOperation::Load ?
							vk::AttachmentLoadOp::eLoad :
							value.depth_stencil->depth_load == LoadOperation::Clear ?
								vk::AttachmentLoadOp::eClear :
								vk::AttachmentLoadOp::eDontCare,
						value.depth_stencil->depth_store == StoreOperation::Store ?
							vk::AttachmentStoreOp::eStore :
							vk::AttachmentStoreOp::eDontCare,
						value.depth_stencil->stencil_load == LoadOperation::Load ?
							vk::AttachmentLoadOp::eLoad :
							value.depth_stencil->stencil_load == LoadOperation::Clear ?
								vk::AttachmentLoadOp::eClear :
								vk::AttachmentLoadOp::eDontCare,
						value.depth_stencil->stencil_store == StoreOperation::Store ?
							vk::AttachmentStoreOp::eStore :
							vk::AttachmentStoreOp::eDontCare,
						depth_layout,
						depth_layout
					);
					depth_reference.emplace(
						attachment,
						depth_layout
					);
					framebuffer_views.emplace_back(*NativeImageView(view));
					clear_values.emplace_back(
						vk::ClearDepthStencilValue(
							value.depth_stencil->clear_depth,
							value.depth_stencil->clear_stencil
						)
					);
				}
				vk::SubpassDescription subpass;
				subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
				subpass.colorAttachmentCount = static_cast<std::uint32_t>(color_references.size());
				subpass.pColorAttachments = color_references.data();
				subpass.pResolveAttachments = has_resolve ? resolve_references.data() : nullptr;
				subpass.pDepthStencilAttachment = depth_reference ? &*depth_reference : nullptr;
				vk::RenderPassCreateInfo render_pass_info(
					vk::RenderPassCreateFlags{},
					attachments,
					subpass
				);
				auto native_render_pass = device->createRenderPass(
					render_pass_info,
					nullptr,
					dispatcher
				);
				vk::SharedRenderPass render_pass(
					native_render_pass,
					device,
					{ nullptr, dispatcher }
				);
				if (!framebuffer_extent) {
					throw std::invalid_argument("Vulkan rendering requires at least one attachment");
				}
				auto render_right = static_cast<std::uint64_t>(value.area.x) + value.area.width;
				auto render_bottom = static_cast<std::uint64_t>(value.area.y) + value.area.height;
				if (value.area.x < 0 || value.area.y < 0 ||
					render_right > framebuffer_extent->width ||
					render_bottom > framebuffer_extent->height) {
					throw std::invalid_argument("Vulkan render area exceeds framebuffer extent");
				}
				vk::FramebufferCreateInfo framebuffer_info(
					vk::FramebufferCreateFlags{},
					native_render_pass,
					framebuffer_views,
					framebuffer_extent->width,
					framebuffer_extent->height,
					1u
				);
				auto native_framebuffer = device->createFramebuffer(
					framebuffer_info,
					nullptr,
					dispatcher
				);
				vk::SharedFramebuffer framebuffer(
					native_framebuffer,
					device,
					{ nullptr, dispatcher }
				);
				vk::RenderPassBeginInfo begin_info(
					native_render_pass,
					native_framebuffer,
					vk::Rect2D(
						vk::Offset2D(value.area.x, value.area.y),
						vk::Extent2D(value.area.width, value.area.height)
					),
					clear_values
				);
				commands.beginRenderPass(
					begin_info,
					vk::SubpassContents::eInline,
					dispatcher
				);
				vk::Viewport viewport(
					static_cast<float>(value.area.x),
					static_cast<float>(value.area.y),
					static_cast<float>(value.area.width),
					static_cast<float>(value.area.height),
					0.0f,
					1.0f
				);
				commands.setViewport(0u, viewport, dispatcher);
				commands.setScissor(0u, begin_info.renderArea, dispatcher);
				retained_render_passes->emplace_back(std::move(render_pass));
				retained_framebuffers->emplace_back(std::move(framebuffer));
				legacy_rendering = true;
				rendering = true;
				return;
			}
			std::vector<vk::RenderingAttachmentInfo> colors;
			colors.reserve(value.colors.size());
			for (auto const& color : value.colors) {
				auto const& view = views[color.view].get();
				vk::RenderingAttachmentInfo attachment;
				attachment.imageView = *NativeImageView(view);
				attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
				attachment.loadOp = color.load == LoadOperation::Load ?
					vk::AttachmentLoadOp::eLoad :
					color.load == LoadOperation::Clear ?
						vk::AttachmentLoadOp::eClear :
						vk::AttachmentLoadOp::eDontCare;
				attachment.storeOp = color.store == StoreOperation::Store ?
					vk::AttachmentStoreOp::eStore :
					vk::AttachmentStoreOp::eDontCare;
				attachment.clearValue.color = vk::ClearColorValue(
					std::array{
						color.clear.red,
						color.clear.green,
						color.clear.blue,
						color.clear.alpha
					}
				);
				if (color.resolve_view) {
					auto const& resolve = views[*color.resolve_view].get();
					attachment.resolveMode = vk::ResolveModeFlagBits::eAverage;
					attachment.resolveImageView = *NativeImageView(resolve);
					attachment.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
				}
				colors.emplace_back(attachment);
			}
			std::optional<vk::RenderingAttachmentInfo> depth;
			std::optional<vk::RenderingAttachmentInfo> stencil;
			if (value.depth_stencil) {
				auto depth_layout = DepthStencilLayout(value.depth_stencil->resource);
				auto const& view = views[value.depth_stencil->view].get();
				vk::RenderingAttachmentInfo attachment;
				attachment.imageView = *NativeImageView(view);
				attachment.imageLayout = depth_layout;
				attachment.loadOp = value.depth_stencil->depth_load == LoadOperation::Load ?
					vk::AttachmentLoadOp::eLoad :
					value.depth_stencil->depth_load == LoadOperation::Clear ?
						vk::AttachmentLoadOp::eClear :
						vk::AttachmentLoadOp::eDontCare;
				attachment.storeOp = value.depth_stencil->depth_store == StoreOperation::Store ?
					vk::AttachmentStoreOp::eStore :
					vk::AttachmentStoreOp::eDontCare;
				attachment.clearValue.depthStencil = vk::ClearDepthStencilValue(
					value.depth_stencil->clear_depth,
					value.depth_stencil->clear_stencil
				);
				depth = attachment;
				if ((view.subresource_range.aspectMask & vk::ImageAspectFlagBits::eStencil) !=
					vk::ImageAspectFlags{}) {
					stencil = attachment;
					stencil->loadOp = value.depth_stencil->stencil_load == LoadOperation::Load ?
						vk::AttachmentLoadOp::eLoad :
						value.depth_stencil->stencil_load == LoadOperation::Clear ?
							vk::AttachmentLoadOp::eClear :
							vk::AttachmentLoadOp::eDontCare;
					stencil->storeOp = value.depth_stencil->stencil_store == StoreOperation::Store ?
						vk::AttachmentStoreOp::eStore :
						vk::AttachmentStoreOp::eDontCare;
				}
			}
			vk::RenderingInfo info;
			info.renderArea = vk::Rect2D(
				vk::Offset2D(value.area.x, value.area.y),
				vk::Extent2D(value.area.width, value.area.height)
			);
			info.layerCount = 1u;
			info.colorAttachmentCount = static_cast<std::uint32_t>(colors.size());
			info.pColorAttachments = colors.data();
			info.pDepthAttachment = depth ? &*depth : nullptr;
			info.pStencilAttachment = stencil ? &*stencil : nullptr;
			commands.beginRendering(info, dispatcher);
			float y = static_cast<float>(value.area.y);
			float height = static_cast<float>(value.area.height);
			// The area-derived fallback viewport uses the canonical Y-up convention
			// (matching D3D12/WebGPU/OpenGL); an explicit Viewport command may
			// override it with a different ClipSpace.
			y = static_cast<float>(value.area.y) + static_cast<float>(value.area.height);
			height = -static_cast<float>(value.area.height);
			vk::Viewport viewport(
				static_cast<float>(value.area.x),
				y,
				static_cast<float>(value.area.width),
				height,
				0.0f,
				1.0f
			);
			commands.setViewport(0u, viewport, dispatcher);
			commands.setScissor(0u, info.renderArea, dispatcher);
			rendering = true;
		}

		void operator()(EndRendering const&) {
			if (!rendering) {
				throw std::logic_error("Vulkan EndRendering without BeginRendering");
			}
			if (legacy_rendering) {
				commands.endRenderPass(dispatcher);
				legacy_rendering = false;
			}
			else {
				commands.endRendering(dispatcher);
			}
			rendering = false;
		}

		void operator()(BindPipeline const& value) {
			pipeline = &pipelines[value.pipeline].get();
			if (legacy_rendering && pipeline->bind_point == vk::PipelineBindPoint::eGraphics) {
				if (!pipeline->compatible_render_pass ||
					pipeline->color_formats != rendering_color_formats ||
					pipeline->depth_stencil_format != rendering_depth_stencil_format ||
					pipeline->samples != rendering_samples) {
					throw std::invalid_argument(
						"Vulkan graphics pipeline is incompatible with the active render pass"
					);
				}
			}
			commands.bindPipeline(
				pipeline->bind_point,
				*pipeline->impl,
				dispatcher
			);
		}

		void operator()(BindResourceGroup const& value) {
			if (!pipeline) {
				throw std::logic_error("Vulkan resource group requires a bound pipeline");
			}
			auto const& group = groups[value.group].get();
			if (*group.layout != *pipeline->layout) {
				throw std::invalid_argument("Vulkan resource group pipeline layout mismatch");
			}
			commands.bindDescriptorSets(
				pipeline->bind_point,
				*pipeline->layout,
				value.index,
				group.set,
				{},
				dispatcher
			);
		}

		void operator()(BindVertexBuffer const& value) {
			auto& buffer = NativeBuffer(resources[value.resource].get());
			vk::Buffer native_buffer(buffer.allocation.GetBuffer());
			vk::DeviceSize offset = value.offset;
			commands.bindVertexBuffers(
				value.slot,
				1u,
				&native_buffer,
				&offset,
				dispatcher
			);
		}

		void operator()(BindIndexBuffer const& value) {
			auto& buffer = NativeBuffer(resources[value.resource].get());
			commands.bindIndexBuffer(
				buffer.allocation.GetBuffer(),
				value.offset,
				value.type == IndexType::Uint16 ?
					vk::IndexType::eUint16 :
					vk::IndexType::eUint32,
				dispatcher
			);
		}

		void operator()(Viewport const& value) {
			float y = value.y;
			float height = value.height;
			if (value.clip_space == ClipSpace::YUp) {
				// Vulkan NDC is Y-down; a negative viewport height flips it so the
				// canonical Y-up convention matches D3D12/WebGPU/OpenGL.
				y = value.y + value.height;
				height = -value.height;
			}
			vk::Viewport viewport(
				value.x,
				y,
				value.width,
				height,
				value.minimum_depth,
				value.maximum_depth
			);
			commands.setViewport(0u, viewport, dispatcher);
		}

		void operator()(Scissor const& value) {
			vk::Rect2D scissor(
				vk::Offset2D(value.x, value.y),
				vk::Extent2D(value.width, value.height)
			);
			commands.setScissor(0u, scissor, dispatcher);
		}

		void operator()(Draw const& value) {
			commands.draw(
				value.vertex_count,
				value.instance_count,
				value.first_vertex,
				value.first_instance,
				dispatcher
			);
		}

		void operator()(DrawIndexed const& value) {
			commands.drawIndexed(
				value.index_count,
				value.instance_count,
				value.first_index,
				value.vertex_offset,
				value.first_instance,
				dispatcher
			);
		}

		void operator()(Dispatch const& value) {
			commands.dispatch(
				value.group_count_x,
				value.group_count_y,
				value.group_count_z,
				dispatcher
			);
		}

		void operator()(CopyBuffer const& value) {
			auto& source = NativeBuffer(resources[value.source].get());
			auto& destination = NativeBuffer(resources[value.destination].get());
			vk::BufferCopy region(
				value.source_offset,
				value.destination_offset,
				value.size
			);
			commands.copyBuffer(
				source.allocation.GetBuffer(),
				destination.allocation.GetBuffer(),
				region,
				dispatcher
			);
		}
		void operator()(WriteBuffer const& value) {
			auto& buffer = NativeBuffer(resources[value.resource].get());
			auto buffer_size = std::get<std::size_t>(
				resource_metadata[value.resource].size_or_extent
			);
			if (value.offset > buffer_size ||
				value.data.size() > buffer_size - value.offset) {
				throw std::out_of_range("Vulkan write exceeds the buffer size");
			}
			buffer.allocation.Write(
				value.offset,
				value.data
			);
		}

		vk::BufferImageCopy BufferImageRegion(
			ResourceMetadata const& texture,
			fyuu_rhi::TextureDataLayout const& layout,
			fyuu_rhi::TextureRegion const& region
		) const {
			auto format = static_cast<vk::Format>(texture.format);
			auto texel_size = TexelSize(format);
			if (layout.bytes_per_row % texel_size != 0u) {
				throw std::invalid_argument("Vulkan texture copy row pitch is not texel aligned");
			}
			return vk::BufferImageCopy(
				layout.offset,
				layout.bytes_per_row / texel_size,
				layout.rows_per_image,
				vk::ImageSubresourceLayers(
					NativeAspect(format),
					region.mip_level,
					region.base_array_layer,
					region.array_layer_count
				),
				vk::Offset3D(region.offset_x, region.offset_y, region.offset_z),
				vk::Extent3D(region.width, region.height, region.depth)
			);
		}

		void operator()(CopyBufferToTexture const& value) {
			auto& source = NativeBuffer(resources[value.source].get());
			auto& destination = NativeTexture(resources[value.destination].get());
			auto region = BufferImageRegion(
				resource_metadata[value.destination],
				value.source_layout,
				value.destination_region
			);
			commands.copyBufferToImage(
				source.allocation.GetBuffer(),
				destination.allocation.GetImage(),
				vk::ImageLayout::eTransferDstOptimal,
				region,
				dispatcher
			);
		}

		void operator()(CopyTextureToBuffer const& value) {
			auto& source = NativeTexture(resources[value.source].get());
			auto& destination = NativeBuffer(resources[value.destination].get());
			auto region = BufferImageRegion(
				resource_metadata[value.source],
				value.destination_layout,
				value.source_region
			);
			commands.copyImageToBuffer(
				source.allocation.GetImage(),
				vk::ImageLayout::eTransferSrcOptimal,
				destination.allocation.GetBuffer(),
				region,
				dispatcher
			);
		}

		void operator()(CopyTexture const& value) {
			auto& source = NativeTexture(resources[value.source].get());
			auto& destination = NativeTexture(resources[value.destination].get());
			auto source_format = resource_metadata[value.source].format;
			auto destination_format = resource_metadata[value.destination].format;
			vk::ImageCopy region(
				vk::ImageSubresourceLayers(
					NativeAspect(source_format),
					value.source_region.mip_level,
					value.source_region.base_array_layer,
					value.source_region.array_layer_count
				),
				vk::Offset3D(
					value.source_region.offset_x,
					value.source_region.offset_y,
					value.source_region.offset_z
				),
				vk::ImageSubresourceLayers(
					NativeAspect(destination_format),
					value.destination_region.mip_level,
					value.destination_region.base_array_layer,
					value.destination_region.array_layer_count
				),
				vk::Offset3D(
					value.destination_region.offset_x,
					value.destination_region.offset_y,
					value.destination_region.offset_z
				),
				vk::Extent3D(
					value.source_region.width,
					value.source_region.height,
					value.source_region.depth
				)
			);
			commands.copyImage(
				source.allocation.GetImage(),
				vk::ImageLayout::eTransferSrcOptimal,
				destination.allocation.GetImage(),
				vk::ImageLayout::eTransferDstOptimal,
				region,
				dispatcher
			);
		}

		void operator()(Present const& value) {
			if (!presentation_works || !presentation_indices ||
				presentation_cursor >= presentation_indices->size()) {
				throw std::logic_error("Vulkan presentation work was not prepared");
			}
			auto& work = (*presentation_works)[
				(*presentation_indices)[presentation_cursor++]
			];
			if (work.source != value.source) {
				throw std::logic_error("Vulkan presentation work does not match its command");
			}
			vk::ImageMemoryBarrier2 acquire;
			acquire.srcStageMask = vk::PipelineStageFlagBits2::eNone;
			acquire.srcAccessMask = vk::AccessFlags2{};
			acquire.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
			acquire.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
			// Swapchain contents are discarded because the graph copies the complete
			// source image. UNDEFINED is legal on every frame and avoids maintaining a
			// second layout table for presentation-owned images.
			acquire.oldLayout = vk::ImageLayout::eUndefined;
			acquire.newLayout = vk::ImageLayout::eTransferDstOptimal;
			acquire.srcQueueFamilyIndex = (std::numeric_limits<std::uint32_t>::max)();
			acquire.dstQueueFamilyIndex = (std::numeric_limits<std::uint32_t>::max)();
			acquire.image = work.back_buffer;
			acquire.subresourceRange = vk::ImageSubresourceRange(
				vk::ImageAspectFlagBits::eColor,
				0u,
				1u,
				0u,
				1u
			);
			vk::DependencyInfo acquire_dependency;
			acquire_dependency.imageMemoryBarrierCount = 1u;
			acquire_dependency.pImageMemoryBarriers = &acquire;
			if (synchronization2_supported) {
				commands.pipelineBarrier2(acquire_dependency, dispatcher);
			}
			else {
				vk::ImageMemoryBarrier legacy;
				legacy.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
				legacy.oldLayout = vk::ImageLayout::eUndefined;
				legacy.newLayout = vk::ImageLayout::eTransferDstOptimal;
				legacy.srcQueueFamilyIndex = acquire.srcQueueFamilyIndex;
				legacy.dstQueueFamilyIndex = acquire.dstQueueFamilyIndex;
				legacy.image = work.back_buffer;
				legacy.subresourceRange = acquire.subresourceRange;
				commands.pipelineBarrier(
					vk::PipelineStageFlagBits::eTopOfPipe,
					vk::PipelineStageFlagBits::eTransfer,
					vk::DependencyFlags{},
					0u,
					nullptr,
					0u,
					nullptr,
					1u,
					&legacy,
					dispatcher
				);
			}
			auto& source = NativeTexture(resources[value.source].get());
			auto const& source_metadata = resource_metadata[value.source];
			auto const& source_extent = TextureExtent(source_metadata);
			vk::ImageSubresourceLayers layers(
				vk::ImageAspectFlagBits::eColor,
				0u,
				0u,
				1u
			);
			if (work.extent.width == source_extent.width &&
				work.extent.height == source_extent.height) {
				vk::ImageCopy region(
					layers,
					vk::Offset3D{},
					layers,
					vk::Offset3D{},
					vk::Extent3D(work.extent.width, work.extent.height, 1u)
				);
				commands.copyImage(
					source.allocation.GetImage(),
					vk::ImageLayout::eTransferSrcOptimal,
					work.back_buffer,
					vk::ImageLayout::eTransferDstOptimal,
					region,
					dispatcher
				);
			}
			else {
				// The swapchain extent follows the surface (see ResolveSwapchainExtent);
				// scale the source into the back buffer. Formats are identical by the
				// swapchain recreation criteria, and every color renderable format
				// supports linear blitting in practice.
				vk::ImageBlit region(
					layers,
					{ vk::Offset3D{}, vk::Offset3D(
						static_cast<std::int32_t>(source_extent.width),
						static_cast<std::int32_t>(source_extent.height),
						1
					) },
					layers,
					{ vk::Offset3D{}, vk::Offset3D(
						static_cast<std::int32_t>(work.extent.width),
						static_cast<std::int32_t>(work.extent.height),
						1
					) }
				);
				commands.blitImage(
					source.allocation.GetImage(),
					vk::ImageLayout::eTransferSrcOptimal,
					work.back_buffer,
					vk::ImageLayout::eTransferDstOptimal,
					region,
					vk::Filter::eLinear,
					dispatcher
				);
			}
			vk::ImageMemoryBarrier2 release = acquire;
			release.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
			release.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
			release.dstStageMask = vk::PipelineStageFlagBits2::eNone;
			release.dstAccessMask = vk::AccessFlags2{};
			release.oldLayout = vk::ImageLayout::eTransferDstOptimal;
			release.newLayout = vk::ImageLayout::ePresentSrcKHR;
			vk::DependencyInfo release_dependency;
			release_dependency.imageMemoryBarrierCount = 1u;
			release_dependency.pImageMemoryBarriers = &release;
			if (synchronization2_supported) {
				commands.pipelineBarrier2(release_dependency, dispatcher);
			}
			else {
				vk::ImageMemoryBarrier legacy;
				legacy.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
				legacy.oldLayout = vk::ImageLayout::eTransferDstOptimal;
				legacy.newLayout = vk::ImageLayout::ePresentSrcKHR;
				legacy.srcQueueFamilyIndex = release.srcQueueFamilyIndex;
				legacy.dstQueueFamilyIndex = release.dstQueueFamilyIndex;
				legacy.image = work.back_buffer;
				legacy.subresourceRange = release.subresourceRange;
				commands.pipelineBarrier(
					vk::PipelineStageFlagBits::eTransfer,
					vk::PipelineStageFlagBits::eBottomOfPipe,
					vk::DependencyFlags{},
					0u,
					nullptr,
					0u,
					nullptr,
					1u,
					&legacy,
					dispatcher
				);
			}
		}
	};

}

namespace fyuu_rhi::vulkan {
	CompletionToken::ManagedCommandBuffer::~ManagedCommandBuffer() noexcept {
		if (!owner || !impl) {
			return;
		}
		try {
			// A command buffer must not be reset while pending. CompletionToken may
			// be destroyed without Poll(), so retirement performs the required wait.
			if (auto timeline = std::get_if<TimelineCompletion>(&completion)) {
				vk::Semaphore semaphore = *timeline->semaphore;
				std::uint64_t value = timeline->value;
				vk::SemaphoreWaitInfo wait_info(
					vk::SemaphoreWaitFlags{},
					1u,
					&semaphore,
					&value
				);
				(void)context->device->waitSemaphores(
					wait_info,
					(std::numeric_limits<std::uint64_t>::max)(),
					*context->dispatcher
				);
			}
			else if (auto binary = std::get_if<BinaryCompletion>(&completion)) {
				(void)context->device->waitForFences(
					*binary->fence,
					true,
					(std::numeric_limits<std::uint64_t>::max)(),
					*context->dispatcher
				);
			}
			(void)impl.reset(
				vk::CommandBufferResetFlags{},
				*context->dispatcher
			);
			CommandPoolContext* pool = nullptr;
			{
				std::unique_lock<std::mutex> pools_lock(context->command_pools_mutex);
				auto found = context->command_pools.find(family);
				if (found == context->command_pools.end()) {
					return;
				}
				pool = &found->second;
			}
			std::unique_lock<std::mutex> lock(pool->mutex);
			pool->command_buffers.emplace_back(impl);
		}
		catch (...) {
			// Device loss makes this command buffer unsafe to recycle.
		}
	}

	CompletionToken::~CompletionToken() noexcept {
		if (!owner) {
			return;
		}
		try {
			for (auto const& fence : presentation_fences) {
				(void)context->device->waitForFences(
					*fence,
					true,
					(std::numeric_limits<std::uint64_t>::max)(),
					*context->dispatcher
				);
			}
		}
		catch (...) {
			// Destructors cannot report device loss; Poll exposes it when observed.
		}
	}

	BinaryCompletionPointAllocator::ManagedBinaryCompletionPoint::~ManagedBinaryCompletionPoint() noexcept {
		if (!owner || !fence || !submitted) {
			return;
		}
		try {
			(void)owner->device->waitForFences(
				*fence,
				true,
				(std::numeric_limits<std::uint64_t>::max)(),
				*owner->dispatcher
			);
			owner->device->resetFences(
				*fence,
				*owner->dispatcher
			);
			std::unique_lock<std::mutex> lock(owner->mutex);
			owner->fences.emplace_back(std::move(fence));
			for (auto& semaphore : consumed_semaphores) {
				owner->semaphores.emplace_back(std::move(semaphore));
			}
		}
		catch (...) {
			// Device loss or teardown makes the native objects unsafe to recycle.
		}
	}

	BinaryCompletionPointAllocator::ManagedBinaryCompletionPoint
		BinaryCompletionPointAllocator::AllocateCompletionPoint(std::size_t consumed_semaphore_count) {
		vk::SharedFence fence;
		std::vector<vk::SharedSemaphore> consumed_semaphores;
		consumed_semaphores.reserve(consumed_semaphore_count);
		{
			std::unique_lock<std::mutex> lock(mutex);
			if (!fences.empty()) {
				fence = std::move(fences.front());
				fences.pop_front();
			}
			while (!semaphores.empty() && consumed_semaphores.size() < consumed_semaphore_count) {
				consumed_semaphores.emplace_back(std::move(semaphores.front()));
				semaphores.pop_front();
			}
		}
		if (!fence) {
			auto native_fence = device->createFence(
				vk::FenceCreateInfo{},
				nullptr,
				*dispatcher
			);
			fence = vk::SharedFence(
				native_fence,
				device,
				{
					nullptr,
					*dispatcher
				}
			);
		}
		while (consumed_semaphores.size() < consumed_semaphore_count) {
			auto native_semaphore = device->createSemaphore(
				vk::SemaphoreCreateInfo{},
				nullptr,
				*dispatcher
			);
			vk::SharedSemaphore semaphore(
				native_semaphore,
				device,
				{
					nullptr,
					*dispatcher
				}
			);
			consumed_semaphores.emplace_back(std::move(semaphore));
		}
		return ManagedBinaryCompletionPoint(
			shared_from_this(),
			fence,
			std::move(consumed_semaphores)
		);
	}

} // namespace fyuu_rhi::vulkan

namespace fyuu_rhi {
	template <>
	struct CreateScheduler<vulkan::LogicalDevice> {
		vulkan::LogicalDevice* logical_device;

		execution::CommandScheduler operator()() const {
			vulkan::CommandSchedulerContext context(
				logical_device->physical_device,
				logical_device->impl,
				logical_device->dispatcher,
				logical_device->queue_alloc
			);
			context.timeline_semaphore_supported = logical_device->enabled_features.contains(
				vk::StructureType::ePhysicalDeviceTimelineSemaphoreFeatures
			);
			context.synchronization2_supported = logical_device->enabled_features.contains(
				vk::StructureType::ePhysicalDeviceSynchronization2Features
			);
			context.dynamic_rendering_supported = logical_device->enabled_features.contains(
				vk::StructureType::ePhysicalDeviceDynamicRenderingFeatures
			);
			if (context.timeline_semaphore_supported) {
				context.completion_points.emplace<
					vulkan::CommandSchedulerContext::TimelineMap
				>();
			}
			else {
				auto allocator = std::make_shared<vulkan::BinaryCompletionPointAllocator>();
				allocator->device = logical_device->impl;
				allocator->dispatcher = logical_device->dispatcher;
				context.completion_points = std::move(allocator);
			}
			return execution::MakeCommandScheduler(
				std::move(context)
			);
		}
	};
} // namespace fyuu_rhi

namespace fyuu_rhi::execution {
	template <>
	struct ExecuteCommands<vulkan::CommandSchedulerContext> {
		vulkan::CommandSchedulerContext* context;
		std::shared_ptr<execution::CommandSchedulerContext> owner;

		CompletionToken operator()(
		ExecutionPlan const& plan,
		std::span<PlatformHandle const> presentation_targets,
		std::span<Resource const> bound_resources,
		std::span<View const> bound_views,
		std::span<Sampler const> bound_samplers,
		std::span<Pipeline const> bound_pipelines,
		std::span<PipelineResourceGroup const> bound_resource_groups,
		StopTokenView stop_token
		) const {
		using namespace vulkan;
		if (!context || !owner) {
			throw std::invalid_argument("Vulkan scheduler is not initialized");
		}
		auto& scheduler = *context;
		auto FindPresentation = [&](std::uint32_t family) -> PresentationContext* {
			std::unique_lock<std::mutex> presentations_lock(
				scheduler.presentations_mutex
			);
			auto found = scheduler.presentations.find(family);
			if (found == scheduler.presentations.end()) {
				return nullptr;
			}
			return &found->second;
		};
		std::vector<std::reference_wrapper<vulkan::Resource>> resources;
		std::vector<ResourceMetadata> resource_metadata;
		std::vector<std::reference_wrapper<vulkan::View>> views;
		std::vector<std::reference_wrapper<vulkan::Sampler>> samplers;
		std::vector<std::reference_wrapper<vulkan::Pipeline>> pipelines;
		std::vector<std::reference_wrapper<vulkan::PipelineResourceGroup>> resource_groups;
		resources.reserve(bound_resources.size());
		resource_metadata.reserve(bound_resources.size());
		views.reserve(bound_views.size());
		samplers.reserve(bound_samplers.size());
		pipelines.reserve(bound_pipelines.size());
		resource_groups.reserve(bound_resource_groups.size());
		std::ranges::transform(
			bound_resources,
			std::back_inserter(resources),
			[](fyuu_rhi::Resource const& resource) -> vulkan::Resource& {
				if (!resource.m_impl) {
					throw std::invalid_argument("A Vulkan execution resource is empty");
				}
				auto native = std::get_if<vulkan::Resource>(&resource.m_impl->native);
				if (!native) {
					throw std::invalid_argument("Vulkan execution received a foreign resource");
				}
				return *native;
			}
		);
		std::ranges::transform(
			bound_resources,
			std::back_inserter(resource_metadata),
			[](fyuu_rhi::Resource const& resource) {
				auto const& size_or_extent = resource.m_impl->size_or_extent;
				auto const& flags = resource.m_impl->flags;
				if (std::holds_alternative<ResourceTextureExtent>(size_or_extent)) {
					return ResourceMetadata{
						size_or_extent,
						vulkan::ResourceFormat(flags),
						vulkan::ImageType(flags),
						vulkan::SampleCount(flags)
					};
				}
				return ResourceMetadata{
					size_or_extent,
					vk::Format::eUndefined,
					vk::ImageType::e2D,
					vk::SampleCountFlagBits::e1
				};
			}
		);
		std::ranges::transform(
			bound_views,
			std::back_inserter(views),
			[](fyuu_rhi::View const& view) -> vulkan::View& {
				if (!view.m_impl) {
					throw std::invalid_argument("A Vulkan execution view is empty");
				}
				auto native = std::get_if<vulkan::View>(&view.m_impl->native);
				if (!native) {
					throw std::invalid_argument("Vulkan execution received a foreign view");
				}
				return *native;
			}
		);
		std::ranges::transform(
			bound_samplers,
			std::back_inserter(samplers),
			[](fyuu_rhi::Sampler const& sampler) -> vulkan::Sampler& {
				if (!sampler.m_impl) {
					throw std::invalid_argument("A Vulkan execution sampler is empty");
				}
				auto native = std::get_if<vulkan::Sampler>(&sampler.m_impl->native);
				if (!native) {
					throw std::invalid_argument("Vulkan execution received a foreign sampler");
				}
				return *native;
			}
		);
		std::ranges::transform(
			bound_pipelines,
			std::back_inserter(pipelines),
			[](fyuu_rhi::Pipeline const& pipeline) -> vulkan::Pipeline& {
				if (!pipeline.m_impl) {
					throw std::invalid_argument("A Vulkan execution pipeline is empty");
				}
				auto native = std::get_if<vulkan::Pipeline>(&pipeline.m_impl->native);
				if (!native) {
					throw std::invalid_argument("Vulkan execution received a foreign pipeline");
				}
				return *native;
			}
		);
		std::ranges::transform(
			bound_resource_groups,
			std::back_inserter(resource_groups),
			[](fyuu_rhi::PipelineResourceGroup const& group) -> vulkan::PipelineResourceGroup& {
				if (!group.m_impl) {
					throw std::invalid_argument("A Vulkan execution resource group is empty");
				}
				auto native = std::get_if<vulkan::PipelineResourceGroup>(
					&group.m_impl->native
				);
				if (!native) {
					throw std::invalid_argument(
						"Vulkan execution received a foreign resource group"
					);
				}
				return *native;
			}
		);
		(void)presentation_targets;
		if (resources.size() != plan.bindings.resource_count ||
			views.size() != plan.bindings.view_count ||
			samplers.size() != plan.bindings.sampler_count ||
			pipelines.size() != plan.bindings.pipeline_count ||
			resource_groups.size() != plan.bindings.resource_group_count) {
			throw std::invalid_argument("Vulkan execution binding count mismatch");
		}
		struct PresentationPreparation {
			std::size_t batch;
			Present command;
			PlatformHandle target;
			vk::SharedSurfaceKHR surface;
			std::vector<std::uint32_t> supported_families;
		};
		std::vector<PresentationPreparation> presentation_preparations;
		std::vector<std::vector<std::uint32_t>> allowed_families(plan.batches.size());
		std::vector<PlatformHandle> active_presentation_targets;
		// Phase 1a: validate all caller-owned data before observing stop. A malformed
		// graph is a deterministic caller error and cancellation must not hide it.
		std::vector<QueueRequest> requests;
		requests.reserve(plan.batches.size());
		for (std::size_t index = 0u; index < plan.batches.size(); ++index) {
			auto const& batch = plan.batches[index];
			if (batch.id != index) {
				throw std::invalid_argument("Vulkan execution batch IDs must match storage indices");
			}
			for (auto dependency : batch.dependencies) {
				if (dependency >= index) {
					throw std::invalid_argument("Vulkan execution batches are not topologically ordered");
				}
			}
			for (auto const& node : batch.nodes) {
				if (node.queue != batch.queue) {
					throw std::invalid_argument("Vulkan execution node and batch queue types differ");
				}
				for (auto const& command : node.commands) {
					if (std::holds_alternative<BeginRendering>(command) &&
						batch.queue != QueueType::Graphics) {
						throw std::invalid_argument("Vulkan BeginRendering requires a graphics queue");
					}
					auto present = std::get_if<Present>(&command);
					if (!present) {
						continue;
					}
					if (present->target >= presentation_targets.size() ||
						present->source >= resources.size()) {
						throw std::invalid_argument("Vulkan presentation binding is invalid");
					}
					auto target = presentation_targets[present->target];
					if (std::ranges::find(active_presentation_targets, target) !=
						active_presentation_targets.end()) {
						throw std::invalid_argument(
							"Vulkan execution cannot present one target more than once"
						);
					}
					active_presentation_targets.emplace_back(target);
					vk::SharedSurfaceKHR surface;
					std::optional<std::uint32_t> pinned_family;
					{
						std::unique_lock<std::mutex> presentations_lock(
							scheduler.presentations_mutex
						);
						for (auto& [family, presentation] : scheduler.presentations) {
							std::unique_lock<std::mutex> presentation_lock(presentation.mutex);
							if (presentation.cache.Contains(target)) {
								pinned_family = family;
								surface = presentation.cache.Get(target).surface;
								break;
							}
						}
					}
					if (!surface) {
						surface = CreateSurface(
							scheduler.physical_device,
							target,
							*scheduler.dispatcher
						);
					}
					auto queue_families = scheduler.physical_device->getQueueFamilyProperties(
						*scheduler.dispatcher
					);
					std::vector<std::uint32_t> supported;
					for (std::uint32_t family = 0u; family < queue_families.size(); ++family) {
						if (scheduler.physical_device->getSurfaceSupportKHR(
							family,
							*surface,
							*scheduler.dispatcher
						)) {
							supported.emplace_back(family);
						}
					}
					if (supported.empty()) {
						throw std::runtime_error("No Vulkan queue family supports the presentation surface");
					}
					if (pinned_family) {
						if (std::ranges::find(supported, *pinned_family) == supported.end()) {
							throw std::runtime_error(
								"Cached Vulkan presentation family no longer supports its surface"
							);
						}
						supported.assign(1u, *pinned_family);
					}
					if (allowed_families[index].empty()) {
						allowed_families[index] = supported;
					}
					else {
						std::erase_if(
							allowed_families[index],
							[&](std::uint32_t family) {
								return std::ranges::find(supported, family) == supported.end();
							}
						);
						if (allowed_families[index].empty()) {
							throw std::runtime_error(
								"No Vulkan queue family supports every target in one presentation batch"
							);
						}
					}
					presentation_preparations.emplace_back(
						index,
						*present,
						target,
						std::move(surface),
						std::move(supported)
					);
				}
			}
			requests.emplace_back(
				NativeCapabilities(batch.queue),
				0.5f,
				EstimateWork(batch),
				std::nullopt,
				allowed_families[index],
				batch.dependencies
			);
		}

		vulkan::CompletionToken token(owner, context);
		if (stop_token.stop_requested()) {
			token.is_cancelled = true;
			return MakeCompletionToken(std::move(token));
		}
		// Concurrent graph execution with the same resource is outside the RHI
		// contract, so resource ownership state needs no per-resource lock. Binary
		// completion still has to retire before its synchronization objects can be
		// replaced by this graph.
		for (auto& resource : resources) {
			if (auto binary = std::get_if<BinaryCompletion>(&resource.get().completion)) {
				// Binary semaphores cannot be host-waited or safely shared across graphs.
				// The producer fence closes the previous graph before this graph creates
				// fresh per-edge binary semaphores.
				(void)scheduler.device->waitForFences(
					*binary->fence,
					true,
					(std::numeric_limits<std::uint64_t>::max)(),
					*scheduler.dispatcher
				);
			}
		}
		for (std::size_t resource = 0u; resource < plan.first_accesses.size(); ++resource) {
			auto const& native = resources[resource].get();
			if (native.owner_family == (std::numeric_limits<std::uint32_t>::max)()) {
				continue;
			}
			for (auto const& access : plan.first_accesses[resource]) {
				if (!requests[access.batch].preferred_family) {
					requests[access.batch].preferred_family = native.owner_family;
				}
			}
		}

		// Phase 1b: QueueAllocator sees the complete graph and atomically selects all
		// physical queues. Scheduler supplies affinity and work estimates but never
		// selects a queue itself.
		auto reservation = scheduler.queue_allocator.Reserve(
			scheduler.device,
			scheduler.dispatcher,
			requests
		);
		std::vector<QueueIdentifier> queue_identifiers;
		queue_identifiers.reserve(plan.batches.size());
		for (std::size_t index = 0u; index < plan.batches.size(); ++index) {
			queue_identifiers.emplace_back(reservation.GetQueue(index).GetIdentifier());
		}
		// owner_family is a permanent queue-family anchor, not the last writer. A
		// previously unowned resource adopts its first graph access family only after
		// successful submission; the provisional value is used while recording this
		// graph's matching acquire/release ownership transfers.
		std::vector<std::uint32_t> resource_anchors(
			resources.size(),
			(std::numeric_limits<std::uint32_t>::max)()
		);
		std::vector<bool> resource_had_anchor(resources.size(), false);
		for (std::size_t resource = 0u; resource < resources.size(); ++resource) {
			auto const& native = resources[resource].get();
			if (native.owner_family !=
				(std::numeric_limits<std::uint32_t>::max)()) {
				resource_anchors[resource] = native.owner_family;
				resource_had_anchor[resource] = true;
			}
			else if (resource < plan.first_accesses.size() &&
				!plan.first_accesses[resource].empty()) {
				resource_anchors[resource] = queue_identifiers[
					plan.first_accesses[resource].front().batch
				].family;
			}
		}

		struct PreparedBatch {
			/// Non-owning plan pointer remains valid for the duration of this call.
			ExecutionBatch const* plan;
			std::size_t command_buffer;
			std::exception_ptr recording_error;
			std::exception_ptr submission_error;
			std::optional<TimelineCompletion> timeline;
			std::optional<std::size_t> binary_completion;
			std::vector<vk::Semaphore> waits;
			std::vector<vk::Semaphore> signals;
			std::vector<TimelineCompletion> external_waits;
			std::vector<vk::SharedRenderPass> render_passes;
			std::vector<vk::SharedFramebuffer> framebuffers;
			std::vector<std::size_t> presentations;
		};

		std::vector<PreparedBatch> prepared;
		prepared.reserve(plan.batches.size());
		token.command_buffers.reserve(plan.batches.size());
		for (std::size_t index = 0u; index < plan.batches.size(); ++index) {
			if (stop_token.stop_requested()) {
				token.is_cancelled = true;
				return MakeCompletionToken(std::move(token));
			}
			auto identifier = reservation.GetQueue(index).GetIdentifier();
			CommandPoolContext* pool = nullptr;
			{
				std::unique_lock<std::mutex> pools_lock(scheduler.command_pools_mutex);
				auto [entry, inserted] = scheduler.command_pools.try_emplace(identifier.family);
				pool = &entry->second;
				if (inserted) {
				vk::CommandPoolCreateInfo info(
					vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
					identifier.family
				);
				auto native_pool = scheduler.device->createCommandPool(
					info,
					nullptr,
					*scheduler.dispatcher
				);
				pool->impl = vk::SharedCommandPool(
					native_pool,
					scheduler.device,
					{
						nullptr,
						*scheduler.dispatcher
					}
				);
				}
			}
			vk::CommandBuffer commands;
			{
				std::unique_lock<std::mutex> pool_lock(pool->mutex);
				if (!pool->command_buffers.empty()) {
					commands = pool->command_buffers.front();
					pool->command_buffers.pop_front();
				}
				else {
					vk::CommandBufferAllocateInfo allocate_info(
						*pool->impl,
						vk::CommandBufferLevel::ePrimary,
						1u
					);
					commands = scheduler.device->allocateCommandBuffers(
						allocate_info,
						*scheduler.dispatcher
					).front();
				}
			}
			auto command_index = token.command_buffers.size();
			token.command_buffers.emplace_back(
				owner,
				context,
				identifier.family,
				commands
			);
			prepared.emplace_back(
				&plan.batches[index],
				command_index
			);
		}
		std::vector<PresentationWork> presentation_works;
		presentation_works.reserve(presentation_preparations.size());
		for (auto& preparation : presentation_preparations) {
			auto family = queue_identifiers[preparation.batch].family;
			if (std::ranges::find(preparation.supported_families, family) ==
				preparation.supported_families.end()) {
				throw std::logic_error("QueueAllocator violated Vulkan present-family restrictions");
			}
			PresentationContext* presentation = nullptr;
			{
				std::unique_lock<std::mutex> presentations_lock(
					scheduler.presentations_mutex
				);
				presentation = &scheduler.presentations.try_emplace(family).first->second;
			}
			std::unique_lock<std::mutex> presentation_lock(presentation->mutex);
			auto const& source = resource_metadata[preparation.command.source];
			if (presentation->cache.Contains(preparation.target)) {
				// oldSwapchain is valid only when the replacement uses the same surface.
				// Reuse the cached surface instead of the equivalent transient surface
				// created earlier for queue-family capability discovery.
				preparation.surface = presentation->cache.Get(
					preparation.target
				).surface;
			}
			auto [requested_mode, fifo_latest_ready_supported] = SelectPresentMode(
				context,
				preparation.surface,
				preparation.command.vertical_sync
			);
			bool recreate = !presentation->cache.Contains(preparation.target);
			PresentationContext::SwapChain const* previous = nullptr;
			if (!recreate) {
				auto& current = presentation->cache.Get(preparation.target);
				previous = &current;
				auto capabilities = scheduler.physical_device->getSurfaceCapabilitiesKHR(
					*preparation.surface,
					*scheduler.dispatcher
				);
				auto expected_extent = ResolveSwapchainExtent(capabilities, source);
				recreate = current.out_of_date ||
					current.present_family != family ||
					current.format != source.format ||
					current.extent.width != expected_extent.width ||
					current.extent.height != expected_extent.height ||
					current.buffer_count != preparation.command.buffer_count ||
					current.present_mode != requested_mode;
			}
			if (recreate) {
				auto replacement = CreateSwapChain(
					context,
					preparation.surface,
					source,
					family,
					preparation.command.buffer_count,
					requested_mode,
					fifo_latest_ready_supported,
					previous
				);
				if (previous) {
					if (previous->swapchain_maintenance1_supported) {
						// Present fences are per-frame WSI retirement points. Waiting all slots
						// makes replacement of the cached old swapchain safe without idling
						// unrelated device queues.
						for (auto const& synchronization : previous->synchronization) {
							(void)scheduler.device->waitForFences(
								*synchronization.fence,
								true,
								(std::numeric_limits<std::uint64_t>::max)(),
								*scheduler.dispatcher
							);
						}
					}
					else {
						// Without maintenance1 no fence denotes actual presentation-engine
						// completion. Configuration changes are rare, so device-idle is the
						// conservative boundary before destroying the retired swapchain.
						scheduler.device->waitIdle(
							*scheduler.dispatcher
						);
					}
				}
				presentation->cache.Put(
					preparation.target,
					std::move(replacement)
				);
			}
			PresentationContext::SwapChain* swapchain = nullptr;
			PresentationContext::BackBufferSynchronization* synchronization = nullptr;
			std::size_t synchronization_index = 0u;
			std::optional<vk::ResultValue<std::uint32_t>> acquired;
			while (!stop_token.stop_requested()) {
				swapchain = &presentation->cache.Get(preparation.target);
				synchronization_index = (
					swapchain->last_back_buffer_index + 1u
				) % swapchain->synchronization.size();
				synchronization = &swapchain->synchronization[synchronization_index];
				(void)scheduler.device->waitForFences(
					*synchronization->fence,
					true,
					(std::numeric_limits<std::uint64_t>::max)(),
					*scheduler.dispatcher
				);
				// Without maintenance1 this fence is passed to acquire and therefore must
				// be unsignaled first. A maintenance1 fence belongs to Present and remains
				// signaled until acquire succeeds, so OUT_OF_DATE cannot strand the slot.
				if (!swapchain->swapchain_maintenance1_supported) {
					scheduler.device->resetFences(
						*synchronization->fence,
						*scheduler.dispatcher
					);
				}
				acquired = scheduler.device->acquireNextImageKHR(
					*swapchain->impl,
					(std::numeric_limits<std::uint64_t>::max)(),
					*synchronization->acquire_semaphore,
					swapchain->swapchain_maintenance1_supported ?
						nullptr :
						*synchronization->fence,
					*scheduler.dispatcher
				);
				if (acquired->result != vk::Result::eErrorOutOfDateKHR) {
					break;
				}
				auto replacement = CreateSwapChain(
					context,
					preparation.surface,
					source,
					family,
					preparation.command.buffer_count,
					requested_mode,
					fifo_latest_ready_supported,
					swapchain
				);
				if (swapchain->swapchain_maintenance1_supported) {
					for (auto const& frame : swapchain->synchronization) {
						(void)scheduler.device->waitForFences(
							*frame.fence,
							true,
							(std::numeric_limits<std::uint64_t>::max)(),
							*scheduler.dispatcher
						);
					}
				}
				else {
					scheduler.device->waitIdle(*scheduler.dispatcher);
				}
				presentation->cache.Put(
					preparation.target,
					std::move(replacement)
				);
			}
			if (stop_token.stop_requested()) {
				break;
			}
			if (
				acquired->result != vk::Result::eSuccess &&
				acquired->result != vk::Result::eSuboptimalKHR
			) {
				throw vk::SystemError(
					acquired->result,
					"Vulkan acquireNextImageKHR failed"
				);
			}
			if (swapchain->swapchain_maintenance1_supported) {
				scheduler.device->resetFences(
					*synchronization->fence,
					*scheduler.dispatcher
				);
			}
			else {
				// The fallback acquire fence proves that WSI finished signaling
				// image_available. Reset it so the post-Present retirement submit can
				// reuse the fence without allocating another synchronization object.
				(void)scheduler.device->waitForFences(
					*synchronization->fence,
					true,
					(std::numeric_limits<std::uint64_t>::max)(),
					*scheduler.dispatcher
				);
				scheduler.device->resetFences(
					*synchronization->fence,
					*scheduler.dispatcher
				);
			}
			auto present_semaphore = scheduler.device->createSemaphore(
				vk::SemaphoreCreateInfo{},
				nullptr,
				*scheduler.dispatcher
			);
			vk::SharedSemaphore present_ready(
				present_semaphore,
				scheduler.device,
				{ nullptr, *scheduler.dispatcher }
			);
			auto work_index = presentation_works.size();
			presentation_works.emplace_back(
				preparation.batch,
				preparation.command.source,
				preparation.target,
				family,
				swapchain->impl,
				swapchain->back_buffers[acquired->value],
				swapchain->extent,
				synchronization->acquire_semaphore,
				std::move(present_ready),
				synchronization->fence,
				acquired->value,
				swapchain->swapchain_maintenance1_supported
			);
			prepared[preparation.batch].presentations.emplace_back(work_index);
			prepared[preparation.batch].waits.emplace_back(
				*presentation_works.back().image_available
			);
			prepared[preparation.batch].signals.emplace_back(
				*presentation_works.back().present_ready
			);
			swapchain->current_back_buffer_index = acquired->value;
			swapchain->last_back_buffer_index = synchronization_index;
			if (acquired->result == vk::Result::eSuboptimalKHR) {
				swapchain->out_of_date = true;
			}
		}
		// A retirement fence reset during acquire but never re-signaled would
		// strand any earlier token still polling it. An empty submission signals
		// it immediately once the device is idle, releasing those references.
		auto ReleaseRetirementFence = [&](PresentationWork const& work) {
			try {
				auto& queue = reservation.GetQueue(work.batch);
				std::unique_lock<std::mutex> queue_lock(queue.GetSubmissionMutex());
				queue.GetNativeQueue()->submit(
					vk::SubmitInfo{},
					*work.retirement_fence,
					*scheduler.dispatcher
				);
			}
			catch (...) {
				// Device loss completes outstanding tokens through the error channel.
			}
		};
		auto DiscardAcquiredPresentations = [&]() {
			if (!presentation_works.empty()) {
				// Error/cancellation paths may overlap an earlier Present that still owns
				// cache synchronization. Performance is irrelevant here; device-idle is
				// the only portable destruction boundary without present-wait support.
				scheduler.device->waitIdle(
					*scheduler.dispatcher
				);
			}
			for (auto const& work : presentation_works) {
				ReleaseRetirementFence(work);
				auto presentation = FindPresentation(work.family);
				if (!presentation) {
					continue;
				}
				std::unique_lock<std::mutex> presentation_lock(presentation->mutex);
				presentation->cache.Erase(work.target);
			}
		};
		if (stop_token.stop_requested()) {
			DiscardAcquiredPresentations();
			token.is_cancelled = true;
			return MakeCompletionToken(std::move(token));
		}
		for (std::size_t resource = 0u; resource < plan.first_accesses.size(); ++resource) {
			if (auto timeline = std::get_if<TimelineCompletion>(
				&resources[resource].get().completion
			)) {
				for (auto const& access : plan.first_accesses[resource]) {
					prepared[access.batch].external_waits.emplace_back(*timeline);
				}
			}
		}

		if (scheduler.timeline_semaphore_supported) {
			// Reserve this graph's timeline values as one short transaction. Recording
			// and submission do not hold the completion-point registry lock.
			std::unique_lock<std::mutex> completion_points_lock(
				scheduler.completion_points_mutex
			);
			auto& timelines = std::get<vulkan::CommandSchedulerContext::TimelineMap>(
				scheduler.completion_points
			);
			for (std::size_t index = 0u; index < prepared.size(); ++index) {
				auto identifier = reservation.GetQueue(index).GetIdentifier();
				auto [timeline, inserted] = timelines.try_emplace(identifier);
				if (inserted) {
					vk::SemaphoreTypeCreateInfo type_info(
						vk::SemaphoreType::eTimeline,
						0u
					);
					vk::SemaphoreCreateInfo info;
					info.pNext = &type_info;
					auto native = scheduler.device->createSemaphore(
						info,
						nullptr,
						*scheduler.dispatcher
					);
					timeline->second.semaphore = vk::SharedSemaphore(
						native,
						scheduler.device,
						{
							nullptr,
							*scheduler.dispatcher
						}
					);
				}
				++timeline->second.value;
				prepared[index].timeline = timeline->second;
			}
		}
		else {
			// A binary semaphore belongs to its consumer batch. The consumer fence is
			// the first point proving the wait executed and therefore the only safe
			// retirement point for the semaphore.
			auto allocator = std::get<std::shared_ptr<BinaryCompletionPointAllocator>>(
				scheduler.completion_points
			);
			token.binary_completions.reserve(
				prepared.size() + plan.last_accesses.size()
			);
			for (std::size_t index = 0u; index < prepared.size(); ++index) {
				auto destination = reservation.GetQueue(index).GetIdentifier();
				auto incoming = std::ranges::count_if(
					prepared[index].plan->dependencies,
					[&](std::size_t dependency) {
						return reservation.GetQueue(dependency).GetIdentifier() != destination;
					}
				);
				prepared[index].binary_completion = token.binary_completions.size();
				token.binary_completions.emplace_back(
					allocator->AllocateCompletionPoint(incoming)
				);
			}
			for (std::size_t index = 0u; index < prepared.size(); ++index) {
				auto destination = reservation.GetQueue(index).GetIdentifier();
				auto& completion = token.binary_completions[
					*prepared[index].binary_completion
				];
				std::size_t semaphore_index = 0u;
				for (auto dependency : prepared[index].plan->dependencies) {
					if (reservation.GetQueue(dependency).GetIdentifier() == destination) {
						continue;
					}
					auto semaphore = *completion.consumed_semaphores[semaphore_index++];
					prepared[index].waits.emplace_back(semaphore);
					prepared[dependency].signals.emplace_back(semaphore);
				}
			}
		}

		struct ResourceJoin {
			std::size_t resource;
			std::size_t target_batch;
			std::vector<std::size_t> terminal_batches;
			std::optional<TimelineCompletion> timeline;
			std::optional<std::size_t> binary_completion;
			std::vector<vk::Semaphore> waits;
			bool submitted = false;
		};
		std::vector<ResourceJoin> resource_joins;
		for (std::size_t resource = 0u; resource < plan.last_accesses.size(); ++resource) {
			std::vector<std::size_t> terminals;
			for (auto const& access : plan.last_accesses[resource]) {
				if (std::ranges::find(terminals, access.batch) == terminals.end()) {
					terminals.emplace_back(access.batch);
				}
			}
			if (terminals.size() < 2u) {
				continue;
			}
			auto target = terminals.back();
			auto target_queue = queue_identifiers[target];
			bool multiple_queues = std::ranges::any_of(
				terminals,
				[&](std::size_t batch) {
					return queue_identifiers[batch] != target_queue;
				}
			);
			if (!multiple_queues) {
				continue;
			}
			ResourceJoin join{
				.resource = resource,
				.target_batch = target,
				.terminal_batches = std::move(terminals)
			};
			if (scheduler.timeline_semaphore_supported) {
				std::unique_lock<std::mutex> completion_points_lock(
					scheduler.completion_points_mutex
				);
				auto& timelines = std::get<vulkan::CommandSchedulerContext::TimelineMap>(
					scheduler.completion_points
				);
				auto timeline = timelines.find(target_queue);
				++timeline->second.value;
				join.timeline = timeline->second;
			}
			else {
				auto allocator = std::get<std::shared_ptr<BinaryCompletionPointAllocator>>(
					scheduler.completion_points
				);
				auto incoming = std::ranges::count_if(
					join.terminal_batches,
					[&](std::size_t batch) {
						return queue_identifiers[batch] != target_queue;
					}
				);
				join.binary_completion = token.binary_completions.size();
				token.binary_completions.emplace_back(
					allocator->AllocateCompletionPoint(incoming)
				);
				auto& completion = token.binary_completions.back();
				std::size_t semaphore = 0u;
				for (auto batch : join.terminal_batches) {
					if (queue_identifiers[batch] == target_queue) {
						continue;
					}
					auto native = *completion.consumed_semaphores[semaphore++];
					join.waits.emplace_back(native);
					prepared[batch].signals.emplace_back(native);
				}
			}
			resource_joins.emplace_back(std::move(join));
		}

		// Phase 2: command buffers are independent after serial preparation. A
		// recording exception is stored in its batch and returned as a vacuous token.
		std::atomic_bool cancelled = false;
		ParallelFor(
			std::size_t{ 0u },
			prepared.size(),
			[&](std::size_t index) {
				if (cancelled.load(std::memory_order_acquire) || stop_token.stop_requested()) {
					cancelled.store(true, std::memory_order_release);
					return;
				}
				try {
					auto const& managed = token.command_buffers[prepared[index].command_buffer];
					CommandPoolContext* pool = nullptr;
					{
						std::unique_lock<std::mutex> pools_lock(scheduler.command_pools_mutex);
						pool = &scheduler.command_pools.find(managed.family)->second;
					}
					// Vulkan requires command buffers allocated from the same command
					// pool not to be recorded concurrently. Serialize recording per
					// queue family so distinct families still record in parallel.
					std::unique_lock<std::mutex> recording_lock(pool->mutex);
					auto commands = managed.impl;
					commands.begin(
						vk::CommandBufferBeginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit),
						*scheduler.dispatcher
					);
					Recorder recorder{
						resources,
						resource_metadata,
						views,
						pipelines,
						resource_groups,
						commands,
						*scheduler.dispatcher,
						scheduler.device,
						scheduler.dynamic_rendering_supported,
						scheduler.synchronization2_supported,
						&prepared[index].render_passes,
						&prepared[index].framebuffers,
						&presentation_works,
						&prepared[index].presentations
					};
					for (auto const& node : prepared[index].plan->nodes) {
						recorder.node = &node;
						for (std::size_t resource = 0u; resource < plan.first_accesses.size(); ++resource) {
							for (auto const& access : plan.first_accesses[resource]) {
								if (access.batch != index || access.node != node.id) {
									continue;
								}
								auto stable = NativeAccess{
									vk::PipelineStageFlagBits2::eTopOfPipe,
									vk::AccessFlags2{},
									vk::ImageLayout::eUndefined
								};
								if (resource_had_anchor[resource]) {
									stable.layout = vk::ImageLayout::eGeneral;
								}
								auto anchor = resource_anchors[resource];
								auto family = queue_identifiers[index].family;
								if (resource_had_anchor[resource] &&
									anchor != (std::numeric_limits<std::uint32_t>::max)() &&
									anchor != family) {
									// The previous graph released ownership in the stable layout.
									// Acquire ownership first, then perform this graph's layout/access
									// transition as an ordinary same-family barrier.
									EmitBarrier(
										commands,
										resources[resource].get(),
										resource_metadata[resource],
										stable,
										stable,
										access.range,
										anchor,
										family,
										scheduler.synchronization2_supported,
										*scheduler.dispatcher
									);
								}
								EmitBarrier(
									commands,
									resources[resource].get(),
									resource_metadata[resource],
									stable,
									MapAccess(access.usage, access.mode),
									access.range,
									(std::numeric_limits<std::uint32_t>::max)(),
									(std::numeric_limits<std::uint32_t>::max)(),
									scheduler.synchronization2_supported,
									*scheduler.dispatcher
								);
							}
						}
						for (auto const& barrier : prepared[index].plan->barriers) {
							if (barrier.destination_node != node.id) {
								continue;
							}
							auto source_family = queue_identifiers[barrier.source_batch].family;
							auto destination_family = queue_identifiers[barrier.destination_batch].family;
							EmitBarrier(
								commands,
								resources[barrier.resource].get(),
								resource_metadata[barrier.resource],
								MapAccess(barrier.source_usage, barrier.source_mode),
								MapAccess(barrier.destination_usage, barrier.destination_mode),
								barrier.destination_range,
								source_family == destination_family ?
								(std::numeric_limits<std::uint32_t>::max)() :
								source_family,
								source_family == destination_family ?
								(std::numeric_limits<std::uint32_t>::max)() :
								destination_family,
								scheduler.synchronization2_supported,
								*scheduler.dispatcher
							);
						}
						for (auto const& command : node.commands) {
							std::visit(recorder, command);
						}
						for (auto const& barrier : prepared[index].plan->release_barriers) {
							if (barrier.source_node != node.id) {
								continue;
							}
							auto source_family = queue_identifiers[barrier.source_batch].family;
							auto destination_family = queue_identifiers[barrier.destination_batch].family;
							if (source_family != destination_family) {
								EmitBarrier(
									commands,
									resources[barrier.resource].get(),
									resource_metadata[barrier.resource],
									MapAccess(barrier.source_usage, barrier.source_mode),
									MapAccess(barrier.destination_usage, barrier.destination_mode),
									barrier.source_range,
									source_family,
									destination_family,
									scheduler.synchronization2_supported,
									*scheduler.dispatcher
								);
							}
						}
						for (std::size_t resource = 0u; resource < plan.last_accesses.size(); ++resource) {
							for (auto const& access : plan.last_accesses[resource]) {
								if (access.batch != index || access.node != node.id) {
									continue;
								}
								auto stable = NativeAccess{
									vk::PipelineStageFlagBits2::eBottomOfPipe,
									vk::AccessFlags2{},
									vk::ImageLayout::eUndefined
								};
								if (std::holds_alternative<ResourceTextureExtent>(
									resource_metadata[resource].size_or_extent
								)) {
									stable.layout = vk::ImageLayout::eGeneral;
								}
								EmitBarrier(
									commands,
									resources[resource].get(),
									resource_metadata[resource],
									MapAccess(access.usage, access.mode),
									stable,
									access.range,
									(std::numeric_limits<std::uint32_t>::max)(),
									(std::numeric_limits<std::uint32_t>::max)(),
									scheduler.synchronization2_supported,
									*scheduler.dispatcher
								);
								auto anchor = resource_anchors[resource];
								auto family = queue_identifiers[index].family;
								if (anchor != (std::numeric_limits<std::uint32_t>::max)() &&
									anchor != family) {
									EmitBarrier(
										commands,
										resources[resource].get(),
										resource_metadata[resource],
										stable,
										stable,
										access.range,
										family,
										anchor,
										scheduler.synchronization2_supported,
										*scheduler.dispatcher
									);
								}
							}
						}
					}
					commands.end(*scheduler.dispatcher);
				}
				catch (...) {
					prepared[index].recording_error = std::current_exception();
				}
			}
		);

		for (auto const& batch : prepared) {
			if (batch.recording_error) {
				DiscardAcquiredPresentations();
				token.exception = batch.recording_error;
				return MakeCompletionToken(std::move(token));
			}
		}
		if (cancelled.load(std::memory_order_acquire) || stop_token.stop_requested()) {
			DiscardAcquiredPresentations();
			token.is_cancelled = true;
			return MakeCompletionToken(std::move(token));
		}
		for (auto& batch : prepared) {
			for (auto& render_pass : batch.render_passes) {
				token.render_passes.emplace_back(std::move(render_pass));
			}
			for (auto& framebuffer : batch.framebuffers) {
				token.framebuffers.emplace_back(std::move(framebuffer));
			}
		}

		// Phase 3: group by physical QueueIdentifier rather than logical QueueType.
		// QueueAllocator may assign two logical peers to different queue indices,
		// while different logical roles may resolve to the same physical queue.
		std::vector<std::vector<std::size_t>> queue_batches;
		std::unordered_map<QueueIdentifier, std::size_t> queue_indices;
		for (std::size_t index = 0u; index < prepared.size(); ++index) {
			auto identifier = reservation.GetQueue(index).GetIdentifier();
			auto [queue, inserted] = queue_indices.try_emplace(
				identifier,
				queue_batches.size()
			);
			if (inserted) {
				queue_batches.emplace_back();
			}
			queue_batches[queue->second].emplace_back(index);
		}
		// A worker publishes submission success before a dependent worker constructs
		// a GPU wait. Failed dependencies are cancelled transitively; this prevents
		// waiting for a timeline value or binary semaphore that will never be signaled.
		std::vector<std::atomic<std::uint8_t>> submission_states(prepared.size());
		std::vector<std::optional<QueueWorkToken>> committed_work(prepared.size());
		ParallelFor(
			std::size_t{ 0u },
			queue_batches.size(),
			[&](std::size_t queue_index) {
				auto const& batches = queue_batches[queue_index];
				if (batches.empty()) {
					return;
				}
				auto& queue = reservation.GetQueue(batches.front());
				// Vulkan requires external synchronization for host access to VkQueue.
				// Holding the device-level mutex for this graph's complete queue stream
				// also prevents another scheduler from interleaving inside its FIFO group.
				std::unique_lock<std::mutex> queue_lock(queue.GetSubmissionMutex());
				for (auto index : batches) {
					bool dependency_failed = false;
					for (auto dependency : prepared[index].plan->dependencies) {
						auto state = submission_states[dependency].load(std::memory_order_acquire);
						while (state == 0u) {
							submission_states[dependency].wait(
								state,
								std::memory_order_acquire
							);
							state = submission_states[dependency].load(std::memory_order_acquire);
						}
						if (state == 2u) {
							dependency_failed = true;
							break;
						}
					}
					if (dependency_failed) {
						prepared[index].submission_error = std::make_exception_ptr(
							std::runtime_error("Vulkan batch dependency was not submitted")
						);
						submission_states[index].store(2u, std::memory_order_release);
						submission_states[index].notify_all();
						continue;
					}
					try {
						auto command_buffer = token.command_buffers[prepared[index].command_buffer].impl;
						std::vector<vk::PipelineStageFlags> wait_stages(
							prepared[index].waits.size(),
							vk::PipelineStageFlagBits::eAllCommands
						);
						std::vector<vk::Semaphore> waits = prepared[index].waits;
						std::vector<vk::Semaphore> signals = prepared[index].signals;
						vk::TimelineSemaphoreSubmitInfo timeline_info;
						std::vector<std::uint64_t> wait_values;
						std::vector<std::uint64_t> signal_values;
						if (prepared[index].timeline) {
							for (auto const& external : prepared[index].external_waits) {
								waits.emplace_back(*external.semaphore);
								wait_stages.emplace_back(vk::PipelineStageFlagBits::eAllCommands);
								wait_values.emplace_back(external.value);
							}
							for (auto dependency : prepared[index].plan->dependencies) {
								if (reservation.GetQueue(dependency).GetIdentifier() == queue.GetIdentifier()) {
									continue;
								}
								waits.emplace_back(*prepared[dependency].timeline->semaphore);
								wait_stages.emplace_back(vk::PipelineStageFlagBits::eAllCommands);
								wait_values.emplace_back(prepared[dependency].timeline->value);
							}
							signals.emplace_back(*prepared[index].timeline->semaphore);
							wait_values.resize(waits.size(), 0u);
							signal_values.resize(signals.size(), 0u);
							signal_values.back() = prepared[index].timeline->value;
							timeline_info = vk::TimelineSemaphoreSubmitInfo(
								wait_values,
								signal_values
							);
						}
						vk::SubmitInfo submit_info(
							waits,
							wait_stages,
							command_buffer,
							signals
						);
						submit_info.pNext = prepared[index].timeline ? &timeline_info : nullptr;
						vk::Fence fence;
						if (prepared[index].binary_completion) {
							fence = *token.binary_completions[
								*prepared[index].binary_completion
							].fence;
						}
						queue.GetNativeQueue()->submit(
							submit_info,
							fence,
							*scheduler.dispatcher
						);
						if (prepared[index].timeline) {
							token.command_buffers[prepared[index].command_buffer].completion =
								*prepared[index].timeline;
						}
						else {
							auto& completion = token.binary_completions[
								*prepared[index].binary_completion
							];
							completion.submitted = true;
							token.command_buffers[prepared[index].command_buffer].completion =
								BinaryCompletion(completion.fence);
						}
						committed_work[index].emplace(reservation.Commit(index));
						submission_states[index].store(1u, std::memory_order_release);
						submission_states[index].notify_all();
					}
					catch (...) {
						prepared[index].submission_error = std::current_exception();
						submission_states[index].store(2u, std::memory_order_release);
						submission_states[index].notify_all();
					}
				}
			}
		);

		if (!scheduler.timeline_semaphore_supported) {
			// A skipped consumer must still consume every semaphore whose producer
			// submission succeeded. Otherwise those semaphores remain signaled and
			// cannot be recycled or destroyed safely. Semaphores belonging to failed
			// producers were never submitted and are removed without a GPU wait.
			for (std::size_t index = 0u; index < prepared.size(); ++index) {
				if (submission_states[index].load(std::memory_order_acquire) != 2u ||
					!prepared[index].binary_completion) {
					continue;
				}
				auto& completion = token.binary_completions[
					*prepared[index].binary_completion
				];
				std::vector<vk::SharedSemaphore> submitted_semaphores;
				submitted_semaphores.reserve(completion.consumed_semaphores.size());
				std::vector<vk::Semaphore> waits;
				std::size_t semaphore_index = 0u;
				auto destination = queue_identifiers[index];
				for (auto dependency : prepared[index].plan->dependencies) {
					if (queue_identifiers[dependency] == destination) {
						continue;
					}
					if (submission_states[dependency].load(std::memory_order_acquire) == 1u) {
						waits.emplace_back(*completion.consumed_semaphores[semaphore_index]);
						submitted_semaphores.emplace_back(
							std::move(completion.consumed_semaphores[semaphore_index])
						);
					}
					++semaphore_index;
				}
				completion.consumed_semaphores = std::move(submitted_semaphores);
				if (waits.empty()) {
					continue;
				}
				try {
					auto& queue = reservation.GetQueue(index);
					std::unique_lock<std::mutex> queue_lock(queue.GetSubmissionMutex());
					std::vector<vk::PipelineStageFlags> stages(
						waits.size(),
						vk::PipelineStageFlagBits::eAllCommands
					);
					vk::SubmitInfo cleanup(
						waits,
						stages,
						{},
						{}
					);
					queue.GetNativeQueue()->submit(
						cleanup,
						*completion.fence,
						*scheduler.dispatcher
					);
					completion.submitted = true;
				}
				catch (...) {
					if (!prepared[index].submission_error) {
						prepared[index].submission_error = std::current_exception();
					}
				}
			}
		}

		for (std::size_t index = 0u; index < prepared.size(); ++index) {
			if (prepared[index].timeline && submission_states[index].load(std::memory_order_acquire) == 1u) {
				token.timeline_completions.emplace_back(*prepared[index].timeline);
			}
			if (committed_work[index]) {
				token.work_tokens.emplace_back(std::move(*committed_work[index]));
			}
		}
		for (auto const& batch : prepared) {
			if (batch.submission_error) {
				token.exception = batch.submission_error;
				break;
			}
		}
		if (!presentation_works.empty()) {
			std::unordered_map<QueueIdentifier, std::vector<std::size_t>> present_groups;
			auto has_abandoned_acquire = std::ranges::any_of(
				presentation_works,
				[&](PresentationWork const& work) {
					return submission_states[work.batch].load(std::memory_order_acquire) != 1u;
				}
			);
			if (has_abandoned_acquire) {
				scheduler.device->waitIdle(
					*scheduler.dispatcher
				);
			}
			for (std::size_t index = 0u; index < presentation_works.size(); ++index) {
				auto const& work = presentation_works[index];
				if (submission_states[work.batch].load(std::memory_order_acquire) == 1u) {
					present_groups[queue_identifiers[work.batch]].emplace_back(index);
				}
				else {
					// The acquire semaphore was never consumed by a queue submission. Remove
					// the whole cache entry so neither the acquired image nor its frame-slot
					// synchronization can be reused by a later execution. Signal the reset
					// retirement fence first so earlier tokens polling it can complete.
					ReleaseRetirementFence(work);
					auto presentation = FindPresentation(work.family);
					std::unique_lock<std::mutex> presentation_lock(
						presentation->mutex
					);
					presentation->cache.Erase(work.target);
				}
			}
			for (auto const& [identifier, work_indices] : present_groups) {
				try {
					auto& queue = reservation.GetQueue(
						presentation_works[work_indices.front()].batch
					);
					std::unique_lock<std::mutex> queue_lock(queue.GetSubmissionMutex());
					std::vector<vk::Semaphore> waits;
					std::vector<vk::SwapchainKHR> swapchains;
					std::vector<std::uint32_t> image_indices;
					std::vector<vk::Fence> present_fences;
					std::vector<vk::Result> results(work_indices.size());
					waits.reserve(work_indices.size());
					swapchains.reserve(work_indices.size());
					image_indices.reserve(work_indices.size());
					present_fences.reserve(work_indices.size());
					auto use_present_fences = std::ranges::all_of(
						work_indices,
						[&presentation_works](std::size_t work_index) {
							return presentation_works[work_index]
								.swapchain_maintenance1_supported;
						}
					);
					for (auto work_index : work_indices) {
						auto const& work = presentation_works[work_index];
						waits.emplace_back(*work.present_ready);
						swapchains.emplace_back(*work.swapchain);
						image_indices.emplace_back(work.image_index);
						if (use_present_fences) {
							present_fences.emplace_back(*work.retirement_fence);
						}
					}
					vk::PresentInfoKHR present_info(
						waits,
						swapchains,
						image_indices,
						results
					);
					vk::SwapchainPresentFenceInfoKHR present_fence_info;
					if (use_present_fences) {
						// maintenance1 makes each fence track the corresponding swapchain's
						// presentation completion. This is stronger than queue submission
						// completion and is the exact lifetime boundary for the frame slot.
						present_fence_info.swapchainCount = static_cast<std::uint32_t>(
							present_fences.size()
						);
						present_fence_info.pFences = present_fences.data();
						present_info.pNext = &present_fence_info;
					}
					auto aggregate = queue.GetNativeQueue()->presentKHR(
						present_info,
						*scheduler.dispatcher
					);
					for (std::size_t position = 0u; position < work_indices.size(); ++position) {
						auto& work = presentation_works[work_indices[position]];
						auto result = results[position];
						if (result == vk::Result::eErrorOutOfDateKHR ||
							aggregate == vk::Result::eErrorOutOfDateKHR) {
							auto presentation = FindPresentation(work.family);
							std::unique_lock<std::mutex> presentation_lock(
								presentation->mutex
							);
							if (presentation->cache.Contains(work.target)) {
								presentation->cache.Get(work.target).out_of_date = true;
							}
						}
						else if (result != vk::Result::eSuccess &&
							result != vk::Result::eSuboptimalKHR) {
							throw vk::SystemError(result, "Vulkan queue present failed");
						}
						if (!use_present_fences) {
							// Without maintenance1 Vulkan exposes no Present fence. A following
							// empty submission provides the best available queue-side retirement
							// point for the transient present-ready semaphore and frame slot.
							queue.GetNativeQueue()->submit(
								vk::SubmitInfo{},
								*work.retirement_fence,
								*scheduler.dispatcher
							);
						}
						token.presentation_semaphores.emplace_back(work.present_ready);
						token.presentation_fences.emplace_back(work.retirement_fence);
					}
				}
				catch (...) {
					if (!token.exception) {
						token.exception = std::current_exception();
					}
					// Present may have consumed only a prefix of the group. Drain the device
					// before erasing every affected cache entry so no swapchain or WSI
					// synchronization object is destroyed while still referenced.
					scheduler.device->waitIdle(
						*scheduler.dispatcher
					);
					for (auto work_index : work_indices) {
						auto const& work = presentation_works[work_index];
						// Unconsumed presents leave their retirement fence reset; signal it
						// so earlier tokens polling the frame slot can still complete.
						ReleaseRetirementFence(work);
						auto presentation = FindPresentation(work.family);
						if (presentation) {
							std::unique_lock<std::mutex> presentation_lock(
								presentation->mutex
							);
							presentation->cache.Erase(work.target);
						}
					}
					break;
				}
			}
		}
		if (!token.exception) {
			// A resource with terminal work on several physical queues needs one
			// publishable completion. The hidden empty submission joins those queue
			// streams without adding an artificial command buffer or CPU wait.
			for (auto& join : resource_joins) {
				try {
					auto& queue = reservation.GetQueue(join.target_batch);
					std::unique_lock<std::mutex> queue_lock(queue.GetSubmissionMutex());
					std::vector<vk::Semaphore> waits = join.waits;
					std::vector<vk::PipelineStageFlags> stages(
						waits.size(),
						vk::PipelineStageFlagBits::eAllCommands
					);
					std::vector<vk::Semaphore> signals;
					std::vector<std::uint64_t> wait_values;
					std::vector<std::uint64_t> signal_values;
					vk::TimelineSemaphoreSubmitInfo timeline_info;
					if (join.timeline) {
						for (auto batch : join.terminal_batches) {
							if (queue_identifiers[batch] == queue.GetIdentifier()) {
								continue;
							}
							waits.emplace_back(*prepared[batch].timeline->semaphore);
							stages.emplace_back(vk::PipelineStageFlagBits::eAllCommands);
							wait_values.emplace_back(prepared[batch].timeline->value);
						}
						signals.emplace_back(*join.timeline->semaphore);
						wait_values.resize(waits.size(), 0u);
						signal_values.emplace_back(join.timeline->value);
						timeline_info = vk::TimelineSemaphoreSubmitInfo(
							wait_values,
							signal_values
						);
					}
					vk::SubmitInfo submit_info(
						waits,
						stages,
						{},
						signals
					);
					submit_info.pNext = join.timeline ? &timeline_info : nullptr;
					vk::Fence fence;
					if (join.binary_completion) {
						fence = *token.binary_completions[*join.binary_completion].fence;
					}
					queue.GetNativeQueue()->submit(
						submit_info,
						fence,
						*scheduler.dispatcher
					);
					if (join.timeline) {
						token.timeline_completions.emplace_back(*join.timeline);
					}
					else {
						token.binary_completions[*join.binary_completion].submitted = true;
					}
					join.submitted = true;
				}
				catch (...) {
					token.exception = std::current_exception();
					break;
				}
			}
		}

		bool device_drained_after_error = false;
		if (token.exception) {
			// Partial submission can leave both binary semaphores and resources without
			// a single publishable join point. Draining is an error-only recovery path:
			// it retires native synchronization and permits a monostate resource
			// completion for terminal work that did execute.
			try {
				scheduler.device->waitIdle(
					*scheduler.dispatcher
				);
				device_drained_after_error = true;
			}
			catch (...) {
				// Preserve the original phase-3 error. Device-lost makes further state
				// recovery meaningless, while token-owned handles still retain lifetimes.
			}
		}
		// Publish each resource whose complete terminal set was submitted, even if an
		// unrelated batch failed. Skipping the entire transaction would leave CPU
		// ownership/layout bookkeeping behind GPU work that really executed.
		for (std::size_t resource = 0u; resource < plan.last_accesses.size(); ++resource) {
				if (plan.last_accesses[resource].empty()) {
					continue;
				}
				auto terminals_submitted = std::ranges::all_of(
					plan.last_accesses[resource],
					[&](ExecutionBoundaryAccess const& access) {
						return submission_states[access.batch].load(
							std::memory_order_acquire
						) == 1u;
					}
				);
				if (!terminals_submitted) {
					continue;
				}
				auto const& last = plan.last_accesses[resource].back();
				auto joined = std::ranges::find_if(
					resource_joins,
					[resource](ResourceJoin const& join) {
						return join.resource == resource;
					}
				);
				auto completion_batch = joined == resource_joins.end() ?
					last.batch :
					joined->target_batch;
				if (joined != resource_joins.end() && !joined->submitted &&
					!device_drained_after_error) {
					continue;
				}
				auto& native = resources[resource].get();
				// Ownership was released back to the permanent anchor during recording.
				// Never replace it with the completion queue: doing so would turn the
				// anchor into last-writer-wins state and break the next graph's acquire.
				native.owner_family = resource_anchors[resource];
				if (device_drained_after_error) {
					native.completion = std::monostate{};
				}
				else if (joined != resource_joins.end()) {
					native.completion = joined->timeline ?
						CompletionPoint(*joined->timeline) :
						CompletionPoint(BinaryCompletion(
							token.binary_completions[*joined->binary_completion].fence
						));
				}
				else if (prepared[completion_batch].timeline) {
					native.completion = *prepared[completion_batch].timeline;
				}
				else {
					native.completion = BinaryCompletion(
						token.binary_completions[
							*prepared[completion_batch].binary_completion
						].fence
					);
				}
		}
		return MakeCompletionToken(std::move(token));
	}
	};

} // namespace fyuu_rhi::execution
#endif // !defined(__APPLE__)
