module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>
#endif // !defined(__APPLE__)
module fyuu_rhi:vulkan_graph_execution;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import vulkan;
import :vulkan_traits;
import :native_command_graph;
namespace {
	using namespace fyuu_rhi;
	using namespace fyuu_rhi::pipeline;
	using namespace fyuu_rhi::vulkan;
	struct CreateBinarySemaphore {
		Backend::VulkanScheduler::BinarySynchronizationPool const* pool;

		vk::SharedSemaphore operator()() const {
			auto semaphore = pool->device->createSemaphore(
				vk::SemaphoreCreateInfo{},
				nullptr,
				*pool->dispatcher
			);
			return vk::SharedSemaphore(
				semaphore,
				pool->device,
				{ nullptr, *pool->dispatcher }
			);
		}
	};

	struct CreateBinaryFence {
		Backend::VulkanScheduler::BinarySynchronizationPool const* pool;

		vk::SharedFence operator()() const {
			auto fence = pool->device->createFence(
				vk::FenceCreateInfo{},
				nullptr,
				*pool->dispatcher
			);
			return vk::SharedFence(fence, pool->device, { nullptr, *pool->dispatcher });
		}
	};

	struct ResetBinaryFence {
		Backend::VulkanScheduler::BinarySynchronizationPool const* pool;

		void operator()(vk::SharedFence& fence) const {
			std::array fences{ *fence };
			pool->device->resetFences(fences, *pool->dispatcher);
		}
	};

	struct CreateVulkanCommandEntry {
		Backend::VulkanScheduler::QueueState const* queue;

		Backend::VulkanScheduler::CommandEntry operator()() const {
			vk::CommandPoolCreateInfo pool_info(
				vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
				queue->family
			);
			auto pool = queue->device->createCommandPool(pool_info, nullptr, *queue->dispatcher);
			vk::SharedCommandPool shared_pool(
				pool,
				queue->device,
				{ nullptr, *queue->dispatcher }
			);
			vk::CommandBufferAllocateInfo allocation_info(
				*shared_pool,
				vk::CommandBufferLevel::ePrimary,
				1u
			);
			auto buffers = queue->device->allocateCommandBuffers(allocation_info, *queue->dispatcher);
			vk::SharedCommandBuffer command_buffer(
				buffers.front(),
				queue->device,
				shared_pool,
				*queue->dispatcher
			);
			command_buffer->begin(vk::CommandBufferBeginInfo{}, *queue->dispatcher);
			return { shared_pool, command_buffer, true };
		}
	};

	struct ResetVulkanCommandEntry {
		Backend::VulkanScheduler::QueueState const* queue;

		void operator()(Backend::VulkanScheduler::CommandEntry& entry) const {
			if (entry.recording) entry.impl->end(*queue->dispatcher);
			queue->device->resetCommandPool(*entry.command_pool, {}, *queue->dispatcher);
			entry.impl->begin(vk::CommandBufferBeginInfo{}, *queue->dispatcher);
			entry.recording = true;
		}
	};

	vk::SharedSurfaceKHR CreatePresentationSurface(
		Backend::PhysicalDevice const& physical_device,
		Backend::PresentationTarget const& target
	) {
#if defined(_WIN32)
		return Backend::CreateSurface(physical_device.instance, target);
#elif defined(__linux__)
		struct CreateSurface {
			Backend::Instance const* instance;

			vk::SharedSurfaceKHR operator()(Backend::X11PresentationTarget const& value) const {
				return Backend::CreateSurface(*instance, value.display, value.window);
			}

			vk::SharedSurfaceKHR operator()(Backend::WaylandPresentationTarget const& value) const {
				return Backend::CreateSurface(*instance, value.display, value.surface);
			}
		};
		return std::visit(CreateSurface{ &physical_device.instance }, target);
#elif defined(__ANDROID__)
		return Backend::CreateSurface(physical_device.instance, target);
#endif
	}

	vk::PresentModeKHR SelectPresentMode(
		std::span<vk::PresentModeKHR const> modes,
		bool vertical_sync,
		bool fifo_latest_ready
	) noexcept {
		if (vertical_sync) {
			if (fifo_latest_ready &&
				std::ranges::find(modes, vk::PresentModeKHR::eFifoLatestReady) != modes.end()) {
				return vk::PresentModeKHR::eFifoLatestReady;
			}
			return vk::PresentModeKHR::eFifo;
		}
		if (std::ranges::find(modes, vk::PresentModeKHR::eMailbox) != modes.end()) {
			return vk::PresentModeKHR::eMailbox;
		}
		if (std::ranges::find(modes, vk::PresentModeKHR::eImmediate) != modes.end()) {
			return vk::PresentModeKHR::eImmediate;
		}
		return vk::PresentModeKHR::eFifo;
	}

	vk::CompositeAlphaFlagBitsKHR SelectCompositeAlpha(
		vk::CompositeAlphaFlagsKHR supported
	) noexcept {
		static constexpr std::array options{
			vk::CompositeAlphaFlagBitsKHR::eOpaque,
			vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
			vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
			vk::CompositeAlphaFlagBitsKHR::eInherit
		};
		for (auto option : options) {
			if ((supported & option) != vk::CompositeAlphaFlagsKHR{}) {
				return option;
			}
		}
		return vk::CompositeAlphaFlagBitsKHR::eOpaque;
	}

	vk::Format MutableViewFormat(vk::Format format) noexcept {
		switch (format) {
		case vk::Format::eR8G8B8A8Unorm: return vk::Format::eR8G8B8A8Srgb;
		case vk::Format::eR8G8B8A8Srgb: return vk::Format::eR8G8B8A8Unorm;
		case vk::Format::eB8G8R8A8Unorm: return vk::Format::eB8G8R8A8Srgb;
		case vk::Format::eB8G8R8A8Srgb: return vk::Format::eB8G8R8A8Unorm;
		default: return vk::Format::eUndefined;
		}
	}

	std::vector<vk::PresentModeKHR> CompatiblePresentModes(
		Backend::PhysicalDevice const& physical_device,
		vk::SurfaceKHR surface,
		vk::PresentModeKHR present_mode
	) {
		vk::SurfacePresentModeKHR surface_mode;
		surface_mode.presentMode = present_mode;
		vk::PhysicalDeviceSurfaceInfo2KHR surface_info;
		surface_info.pNext = &surface_mode;
		surface_info.surface = surface;
		vk::SurfacePresentModeCompatibilityKHR compatibility;
		vk::SurfaceCapabilities2KHR capabilities;
		capabilities.pNext = &compatibility;
		auto& dispatcher = physical_device.instance.dispatcher;
		auto result = physical_device.impl->getSurfaceCapabilities2KHR(
			&surface_info,
			&capabilities,
			dispatcher
		);
		if (result != vk::Result::eSuccess || compatibility.presentModeCount == 0u) {
			return { present_mode };
		}
		std::vector<vk::PresentModeKHR> modes(compatibility.presentModeCount);
		compatibility.pPresentModes = modes.data();
		result = physical_device.impl->getSurfaceCapabilities2KHR(
			&surface_info,
			&capabilities,
			dispatcher
		);
		if (result != vk::Result::eSuccess) {
			return { present_mode };
		}
		modes.resize(compatibility.presentModeCount);
		return modes;
	}

	Backend::LogicalDevice::PresentationEntry CreatePresentationEntry(
		Backend::VulkanScheduler::Implementation const& scheduler,
		Backend::VulkanScheduler::QueueState const& queue,
		Backend::PresentationTarget const& target,
		Backend::Resource::Texture const& source,
		bool vertical_sync,
		std::uint32_t frames_in_flight
	) {
		auto surface = CreatePresentationSurface(scheduler.physical_device, target);
		auto const& dispatcher = *queue.dispatcher;
		auto physical_device = *scheduler.physical_device.impl;
		if (!physical_device.getSurfaceSupportKHR(queue.family, *surface, dispatcher)) {
			throw std::invalid_argument("Vulkan presentation target is unsupported by the graphics queue");
		}
		auto capabilities = physical_device.getSurfaceCapabilitiesKHR(*surface, dispatcher);
		if ((capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst) ==
			vk::ImageUsageFlags{}) {
			throw std::invalid_argument("Vulkan presentation surface does not support transfer destinations");
		}
		auto formats = physical_device.getSurfaceFormatsKHR(*surface, dispatcher);
		auto source_format = static_cast<vk::Format>(source.buf_info.format);
		bool extended_color_spaces = std::ranges::find(
			scheduler.physical_device.instance.enabled_extensions,
			std::string(vk::EXTSwapchainColorSpaceExtensionName)
		) != scheduler.physical_device.instance.enabled_extensions.end();
		auto preferred_color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
		if (extended_color_spaces && source_format == vk::Format::eR16G16B16A16Sfloat) {
			preferred_color_space = vk::ColorSpaceKHR::eExtendedSrgbLinearEXT;
		}
		else if (extended_color_spaces &&
			(source_format == vk::Format::eA2R10G10B10UnormPack32 ||
				source_format == vk::Format::eA2B10G10R10UnormPack32)) {
			preferred_color_space = vk::ColorSpaceKHR::eHdr10St2084EXT;
		}
		auto MatchesPreferredFormat = [source_format, preferred_color_space](
			vk::SurfaceFormatKHR const& value
		) {
			return value.format == source_format && value.colorSpace == preferred_color_space;
		};
		auto MatchesFormat = [source_format](vk::SurfaceFormatKHR const& value) {
			return value.format == source_format;
		};
		vk::SurfaceFormatKHR selected_format;
		if (formats.size() == 1u && formats.front().format == vk::Format::eUndefined) {
			selected_format = vk::SurfaceFormatKHR(source_format, formats.front().colorSpace);
		}
		else {
			auto selected = std::ranges::find_if(formats, MatchesPreferredFormat);
			if (selected == formats.end()) {
				selected = std::ranges::find_if(formats, MatchesFormat);
			}
			if (selected == formats.end()) {
				throw std::invalid_argument("Vulkan presentation source format is unsupported by the surface");
			}
			selected_format = *selected;
		}
		auto modes = physical_device.getSurfacePresentModesKHR(*surface, dispatcher);
		bool fifo_latest_ready = scheduler.enabled_features.contains(
			vk::StructureType::ePhysicalDevicePresentModeFifoLatestReadyFeaturesKHR
		);
		auto present_mode = SelectPresentMode(modes, vertical_sync, fifo_latest_ready);
		bool swapchain_maintenance = scheduler.enabled_features.contains(
			vk::StructureType::ePhysicalDeviceSwapchainMaintenance1FeaturesKHR
		);
		auto compatible_present_modes = swapchain_maintenance ?
			CompatiblePresentModes(scheduler.physical_device, *surface, present_mode) :
			std::vector<vk::PresentModeKHR>{ present_mode };
		auto requested_extent = vk::Extent2D(source.buf_info.extent.width, source.buf_info.extent.height);
		auto extent = capabilities.currentExtent.width != (std::numeric_limits<std::uint32_t>::max)() ?
			capabilities.currentExtent : vk::Extent2D{
				std::clamp(requested_extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
				std::clamp(requested_extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
			};
		if (extent != requested_extent) {
			throw std::invalid_argument("Vulkan presentation source extent does not match the surface extent");
		}
		auto image_count = std::max(frames_in_flight, capabilities.minImageCount);
		if (capabilities.maxImageCount != 0u) {
			image_count = std::min(image_count, capabilities.maxImageCount);
		}
		vk::SwapchainCreateInfoKHR info;
		info.surface = *surface;
		info.minImageCount = image_count;
		info.imageFormat = selected_format.format;
		info.imageColorSpace = selected_format.colorSpace;
		info.imageExtent = extent;
		info.imageArrayLayers = 1u;
		info.imageUsage = vk::ImageUsageFlagBits::eTransferDst;
		info.imageSharingMode = vk::SharingMode::eExclusive;
		info.preTransform = capabilities.currentTransform;
		info.compositeAlpha = SelectCompositeAlpha(capabilities.supportedCompositeAlpha);
		info.presentMode = present_mode;
		info.clipped = true;
		std::array view_formats{ source_format, MutableViewFormat(source_format) };
		auto view_format_count = view_formats.back() == vk::Format::eUndefined ? 1u : 2u;
		vk::ImageFormatListCreateInfo format_list(
			view_format_count,
			view_formats.data()
		);
		vk::SwapchainPresentModesCreateInfoKHR present_modes(
			static_cast<std::uint32_t>(compatible_present_modes.size()),
			compatible_present_modes.data()
		);
		void* create_chain = nullptr;
		if (scheduler.enabled_extensions.contains(vk::KHRSwapchainMutableFormatExtensionName)) {
			info.flags |= vk::SwapchainCreateFlagBitsKHR::eMutableFormat;
			format_list.pNext = create_chain;
			create_chain = &format_list;
		}
		if (swapchain_maintenance) {
			present_modes.pNext = create_chain;
			create_chain = &present_modes;
		}
		info.pNext = create_chain;
		auto raw_swapchain = queue.device->createSwapchainKHR(info, nullptr, dispatcher);
		vk::SharedSwapchainKHR swapchain(
			raw_swapchain,
			queue.device,
			surface,
			{ nullptr, dispatcher }
		);
		Backend::LogicalDevice::PresentationEntry result{
			.surface = surface,
			.swapchain = swapchain,
			.format = selected_format.format,
			.color_space = selected_format.colorSpace,
			.extent = extent,
			.present_mode = present_mode,
			.compatible_present_modes = compatible_present_modes,
			.requested_frames_in_flight = frames_in_flight
		};
		for (auto image : queue.device->getSwapchainImagesKHR(*swapchain, dispatcher)) {
			auto semaphore = queue.device->createSemaphore(vk::SemaphoreCreateInfo{}, nullptr, dispatcher);
			result.frames.push_back(
				{
					.image = image,
					.render_finished = vk::SharedSemaphore(
						semaphore,
						queue.device,
						{ nullptr, dispatcher }
					)
				}
			);
		}
		return result;
	}

	struct VulkanResourceState {
		vk::PipelineStageFlags2 stages = vk::PipelineStageFlagBits2::eTopOfPipe;
		vk::AccessFlags2 access;
		vk::ImageLayout layout = vk::ImageLayout::eUndefined;
		std::uint32_t family = VK_QUEUE_FAMILY_IGNORED;
	};

	VulkanResourceState GetVulkanResourceState(execution::GraphAccessFlagBits flags) noexcept {
		using Flag = execution::GraphAccessFlagBits;
		VulkanResourceState result;
		result.stages = {};
		if ((flags & Flag::Indirect) != Flag::None) {
			result.stages |= vk::PipelineStageFlagBits2::eDrawIndirect;
			result.access |= vk::AccessFlagBits2::eIndirectCommandRead;
		}
		if ((flags & (Flag::Vertex | Flag::Index)) != Flag::None) {
			result.stages |= vk::PipelineStageFlagBits2::eVertexInput;
			result.access |= (flags & Flag::Index) != Flag::None ?
				vk::AccessFlagBits2::eIndexRead : vk::AccessFlagBits2::eVertexAttributeRead;
		}
		if ((flags & (Flag::Uniform | Flag::Sampled | Flag::Storage)) != Flag::None) {
			result.stages |= vk::PipelineStageFlagBits2::eAllGraphics |
				vk::PipelineStageFlagBits2::eComputeShader;
			if ((flags & Flag::Uniform) != Flag::None) result.access |= vk::AccessFlagBits2::eUniformRead;
			if ((flags & Flag::Sampled) != Flag::None) result.access |= vk::AccessFlagBits2::eShaderSampledRead;
			if ((flags & Flag::Storage) != Flag::None) {
				result.access |= vk::AccessFlagBits2::eShaderStorageRead;
				if ((flags & Flag::Write) != Flag::None) result.access |= vk::AccessFlagBits2::eShaderStorageWrite;
			}
		}
		if ((flags & Flag::ColorAttachment) != Flag::None) {
			result.stages |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
			result.access |= vk::AccessFlagBits2::eColorAttachmentRead;
			if ((flags & Flag::Write) != Flag::None) result.access |= vk::AccessFlagBits2::eColorAttachmentWrite;
			result.layout = vk::ImageLayout::eColorAttachmentOptimal;
		}
		if ((flags & Flag::DepthStencilAttachment) != Flag::None) {
			result.stages |= vk::PipelineStageFlagBits2::eEarlyFragmentTests |
				vk::PipelineStageFlagBits2::eLateFragmentTests;
			result.access |= vk::AccessFlagBits2::eDepthStencilAttachmentRead;
			if ((flags & Flag::Write) != Flag::None) result.access |= vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
			result.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
		}
		if ((flags & Flag::CopySource) != Flag::None) {
			result.stages |= vk::PipelineStageFlagBits2::eCopy;
			result.access |= vk::AccessFlagBits2::eTransferRead;
			result.layout = vk::ImageLayout::eTransferSrcOptimal;
		}
		if ((flags & Flag::CopyDestination) != Flag::None) {
			result.stages |= vk::PipelineStageFlagBits2::eCopy;
			result.access |= vk::AccessFlagBits2::eTransferWrite;
			result.layout = vk::ImageLayout::eTransferDstOptimal;
		}
		if ((flags & Flag::ResolveSource) != Flag::None) {
			result.stages |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
			result.access |= vk::AccessFlagBits2::eColorAttachmentRead;
			result.layout = vk::ImageLayout::eColorAttachmentOptimal;
		}
		if ((flags & Flag::ResolveDestination) != Flag::None) {
			result.stages |= vk::PipelineStageFlagBits2::eColorAttachmentOutput;
			result.access |= vk::AccessFlagBits2::eColorAttachmentWrite;
			result.layout = vk::ImageLayout::eColorAttachmentOptimal;
		}
		if ((flags & Flag::Present) != Flag::None) {
			result.stages |= vk::PipelineStageFlagBits2::eCopy;
			result.access |= vk::AccessFlagBits2::eTransferRead;
			result.layout = vk::ImageLayout::eTransferSrcOptimal;
		}
		if (result.layout == vk::ImageLayout::eUndefined &&
			(flags & (Flag::Sampled | Flag::Storage)) != Flag::None) {
			result.layout = (flags & Flag::Storage) != Flag::None ?
				vk::ImageLayout::eGeneral : vk::ImageLayout::eShaderReadOnlyOptimal;
		}
		if (!result.stages) result.stages = vk::PipelineStageFlagBits2::eAllCommands;
		return result;
	}

	vk::ImageAspectFlags VulkanImageAspect(vk::Format format) noexcept {
		switch (format) {
		case vk::Format::eD16Unorm:
		case vk::Format::eD32Sfloat:
			return vk::ImageAspectFlagBits::eDepth;
		case vk::Format::eD24UnormS8Uint:
		case vk::Format::eD32SfloatS8Uint:
			return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
		default:
			return vk::ImageAspectFlagBits::eColor;
		}
	}

	vk::ImageSubresourceRange VulkanSubresourceRange(
		Backend::Resource::Texture const& texture,
		execution::GraphSubresourceRange const& range
	) noexcept {
		return {
			VulkanImageAspect(static_cast<vk::Format>(texture.buf_info.format)),
			range.base_mip_level,
			range.mip_level_count == 0u ? VK_REMAINING_MIP_LEVELS : range.mip_level_count,
			range.base_array_layer,
			range.array_layer_count == 0u ? VK_REMAINING_ARRAY_LAYERS : range.array_layer_count
		};
	}

	void RecordVulkanBarrier(
		vk::CommandBuffer commands,
		Backend::Resource const& resource,
		VulkanResourceState const& source,
		VulkanResourceState const& destination,
		execution::GraphSubresourceRange const& range,
		bool synchronization2,
		std::shared_ptr<vk::detail::DispatchLoaderDynamic> const& dispatcher
	) {
		if (auto buffer = std::get_if<Backend::Resource::Buffer>(&resource.impl)) {
			if (synchronization2) {
				vk::BufferMemoryBarrier2 barrier(
					source.stages, source.access, destination.stages, destination.access,
					source.family, destination.family, buffer->vk_handle,
					range.offset, range.size == 0u ? VK_WHOLE_SIZE : range.size
				);
				vk::DependencyInfo dependency;
				dependency.bufferMemoryBarrierCount = 1u;
				dependency.pBufferMemoryBarriers = &barrier;
				commands.pipelineBarrier2(dependency, *dispatcher);
			}
			else {
				vk::BufferMemoryBarrier barrier(
					vk::AccessFlags(static_cast<VkAccessFlags>(static_cast<VkAccessFlags2>(source.access))),
					vk::AccessFlags(static_cast<VkAccessFlags>(static_cast<VkAccessFlags2>(destination.access))),
					source.family, destination.family, buffer->vk_handle,
					range.offset, range.size == 0u ? VK_WHOLE_SIZE : range.size
				);
				commands.pipelineBarrier(
					vk::PipelineStageFlags(static_cast<VkPipelineStageFlags>(static_cast<VkPipelineStageFlags2>(source.stages))),
					vk::PipelineStageFlags(static_cast<VkPipelineStageFlags>(static_cast<VkPipelineStageFlags2>(destination.stages))),
					{}, {}, barrier, {}, *dispatcher
				);
			}
			return;
		}
		auto const& texture = std::get<Backend::Resource::Texture>(resource.impl);
		auto subresources = VulkanSubresourceRange(texture, range);
		if (synchronization2) {
			vk::ImageMemoryBarrier2 barrier(
				source.stages, source.access, destination.stages, destination.access,
				source.layout, destination.layout, source.family, destination.family,
				texture.vk_handle, subresources
			);
			vk::DependencyInfo dependency;
			dependency.imageMemoryBarrierCount = 1u;
			dependency.pImageMemoryBarriers = &barrier;
			commands.pipelineBarrier2(dependency, *dispatcher);
		}
		else {
			vk::ImageMemoryBarrier barrier(
				vk::AccessFlags(static_cast<VkAccessFlags>(static_cast<VkAccessFlags2>(source.access))),
				vk::AccessFlags(static_cast<VkAccessFlags>(static_cast<VkAccessFlags2>(destination.access))),
				source.layout, destination.layout, source.family, destination.family,
				texture.vk_handle, subresources
			);
			commands.pipelineBarrier(
				vk::PipelineStageFlags(static_cast<VkPipelineStageFlags>(static_cast<VkPipelineStageFlags2>(source.stages))),
				vk::PipelineStageFlags(static_cast<VkPipelineStageFlags>(static_cast<VkPipelineStageFlags2>(destination.stages))),
				{}, {}, {}, barrier, *dispatcher
			);
		}
	}

	struct VulkanCommandRecorder {
		execution::NativeCommandGraphBindings<Backend> const* bindings;
		vk::CommandBuffer commands;
		vk::SharedDevice const* device;
		std::shared_ptr<vk::detail::DispatchLoaderDynamic> const* dispatcher;
		std::vector<vk::SharedRenderPass>* render_passes;
		std::vector<vk::SharedFramebuffer>* framebuffers;
		std::vector<Backend::GraphExecution::Batch::PresentationRequest>* presentations;
		bool dynamic_rendering;
		mutable bool rendering = false;
		mutable Backend::Pipeline const* pipeline = nullptr;
		mutable execution::BeginRenderingCommand const* pending_rendering = nullptr;
		mutable execution::BeginRenderingCommand const* active_rendering = nullptr;

		Backend::View::Texture const& TextureView(execution::GraphViewID id) const {
			auto const& view = bindings->views[id.value].get();
			auto texture = std::get_if<Backend::View::Texture>(&view.impl);
			if (!texture) throw std::invalid_argument("Vulkan rendering requires a texture view");
			return *texture;
		}

		void BeginTraditionalRendering() const {
			if (!pending_rendering || !pipeline) {
				return;
			}
			if (!pipeline->compatible_render_pass) {
				throw std::invalid_argument("Vulkan pipeline is not compatible with traditional render passes");
			}
			auto const& command = *pending_rendering;
			std::vector<vk::AttachmentDescription> attachments;
			std::vector<vk::AttachmentReference> color_references;
			std::vector<vk::ImageView> attachment_views;
			std::vector<vk::ClearValue> clear_values;
			attachments.reserve(command.colors.size() + (command.depth_stencil ? 1u : 0u));
			color_references.reserve(command.colors.size());
			attachment_views.reserve(attachments.capacity());
			clear_values.reserve(attachments.capacity());
			for (auto const& color : command.colors) {
				auto const& view = TextureView(color.view);
				auto const& resource = std::get<Backend::Resource::Texture>(
					bindings->resources[color.resource.value].get().impl
				);
				auto index = static_cast<std::uint32_t>(attachments.size());
				attachments.emplace_back(
					vk::AttachmentDescriptionFlags{},
					view.info.format,
					static_cast<vk::SampleCountFlagBits>(resource.buf_info.samples),
					color.load ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear,
					color.store ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare,
					vk::AttachmentLoadOp::eDontCare,
					vk::AttachmentStoreOp::eDontCare,
					vk::ImageLayout::eColorAttachmentOptimal,
					vk::ImageLayout::eColorAttachmentOptimal
				);
				color_references.emplace_back(index, vk::ImageLayout::eColorAttachmentOptimal);
				attachment_views.emplace_back(*view.impl);
				vk::ClearValue clear;
				clear.color = std::array{
					color.clear_red, color.clear_green, color.clear_blue, color.clear_alpha
				};
				clear_values.emplace_back(clear);
			}

			std::optional<vk::AttachmentReference> depth_reference;
			if (command.depth_stencil) {
				auto const& depth = *command.depth_stencil;
				auto const& view = TextureView(depth.view);
				auto const& resource = std::get<Backend::Resource::Texture>(
					bindings->resources[depth.resource.value].get().impl
				);
				auto index = static_cast<std::uint32_t>(attachments.size());
				attachments.emplace_back(
					vk::AttachmentDescriptionFlags{},
					view.info.format,
					static_cast<vk::SampleCountFlagBits>(resource.buf_info.samples),
					depth.load_depth ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear,
					depth.store_depth ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare,
					depth.load_stencil ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear,
					depth.store_stencil ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare,
					vk::ImageLayout::eDepthStencilAttachmentOptimal,
					vk::ImageLayout::eDepthStencilAttachmentOptimal
				);
				depth_reference.emplace(index, vk::ImageLayout::eDepthStencilAttachmentOptimal);
				attachment_views.emplace_back(*view.impl);
				vk::ClearValue clear;
				clear.depthStencil = vk::ClearDepthStencilValue{ depth.clear_depth, depth.clear_stencil };
				clear_values.emplace_back(clear);
			}

			vk::SubpassDescription subpass;
			subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
			subpass.colorAttachmentCount = static_cast<std::uint32_t>(color_references.size());
			subpass.pColorAttachments = color_references.data();
			subpass.pDepthStencilAttachment = depth_reference ? &*depth_reference : nullptr;
			std::array subpasses{ subpass };
			vk::RenderPassCreateInfo render_pass_info(
				vk::RenderPassCreateFlags{},
				attachments,
				subpasses
			);
			auto raw_render_pass = (*device)->createRenderPass(
				render_pass_info,
				nullptr,
				**dispatcher
			);
			render_passes->push_back(
				vk::SharedRenderPass(raw_render_pass, *device, { nullptr, **dispatcher })
			);
			vk::FramebufferCreateInfo framebuffer_info(
				{},
				raw_render_pass,
				attachment_views,
				command.width,
				command.height,
				1u
			);
			auto raw_framebuffer = (*device)->createFramebuffer(
				framebuffer_info,
				nullptr,
				**dispatcher
			);
			framebuffers->push_back(
				vk::SharedFramebuffer(raw_framebuffer, *device, { nullptr, **dispatcher })
			);
			vk::RenderPassBeginInfo begin_info(
				raw_render_pass,
				raw_framebuffer,
				vk::Rect2D{
					vk::Offset2D{ command.offset_x, command.offset_y },
					vk::Extent2D{ command.width, command.height }
				},
				clear_values
			);
			commands.beginRenderPass(begin_info, vk::SubpassContents::eInline, **dispatcher);
			active_rendering = pending_rendering;
			pending_rendering = nullptr;
		}

		void ResolveTraditionalAttachments(execution::BeginRenderingCommand const& command) const {
			for (auto const& color : command.colors) {
				if (!color.resolve_view && !color.resolve_resource) {
					continue;
				}
				if (!color.resolve_view || !color.resolve_resource) {
					throw std::invalid_argument("Vulkan resolve requires both a resource and a view");
				}
				auto const& source_view = TextureView(color.view);
				auto const& destination_view = TextureView(*color.resolve_view);
				auto const& source = std::get<Backend::Resource::Texture>(
					bindings->resources[color.resource.value].get().impl
				);
				auto const& destination = std::get<Backend::Resource::Texture>(
					bindings->resources[color.resolve_resource->value].get().impl
				);
				if (source.buf_info.samples == VK_SAMPLE_COUNT_1_BIT ||
					destination.buf_info.samples != VK_SAMPLE_COUNT_1_BIT ||
					source_view.info.format != destination_view.info.format) {
					throw std::invalid_argument("Vulkan resolve attachments are incompatible");
				}

				std::array barriers{
					vk::ImageMemoryBarrier(
						vk::AccessFlagBits::eColorAttachmentWrite,
						vk::AccessFlagBits::eTransferRead,
						vk::ImageLayout::eColorAttachmentOptimal,
						vk::ImageLayout::eTransferSrcOptimal,
						VK_QUEUE_FAMILY_IGNORED,
						VK_QUEUE_FAMILY_IGNORED,
						source.vk_handle,
						source_view.info.subresourceRange
					),
					vk::ImageMemoryBarrier(
						vk::AccessFlagBits::eColorAttachmentWrite,
						vk::AccessFlagBits::eTransferWrite,
						vk::ImageLayout::eColorAttachmentOptimal,
						vk::ImageLayout::eTransferDstOptimal,
						VK_QUEUE_FAMILY_IGNORED,
						VK_QUEUE_FAMILY_IGNORED,
						destination.vk_handle,
						destination_view.info.subresourceRange
					)
				};
				commands.pipelineBarrier(
					vk::PipelineStageFlagBits::eColorAttachmentOutput,
					vk::PipelineStageFlagBits::eTransfer,
					{},
					{},
					{},
					barriers,
					**dispatcher
				);
				vk::ImageResolve region(
					vk::ImageSubresourceLayers(
						vk::ImageAspectFlagBits::eColor,
						source_view.info.subresourceRange.baseMipLevel,
						source_view.info.subresourceRange.baseArrayLayer,
						1u
					),
					vk::Offset3D{ command.offset_x, command.offset_y, 0 },
					vk::ImageSubresourceLayers(
						vk::ImageAspectFlagBits::eColor,
						destination_view.info.subresourceRange.baseMipLevel,
						destination_view.info.subresourceRange.baseArrayLayer,
						1u
					),
					vk::Offset3D{ command.offset_x, command.offset_y, 0 },
					vk::Extent3D{ command.width, command.height, 1u }
				);
				std::array regions{ region };
				commands.resolveImage(
					source.vk_handle,
					vk::ImageLayout::eTransferSrcOptimal,
					destination.vk_handle,
					vk::ImageLayout::eTransferDstOptimal,
					regions,
					**dispatcher
				);
				barriers[0].srcAccessMask = vk::AccessFlagBits::eTransferRead;
				barriers[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
					vk::AccessFlagBits::eColorAttachmentWrite;
				barriers[0].oldLayout = vk::ImageLayout::eTransferSrcOptimal;
				barriers[0].newLayout = vk::ImageLayout::eColorAttachmentOptimal;
				barriers[1].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
				barriers[1].dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead |
					vk::AccessFlagBits::eColorAttachmentWrite;
				barriers[1].oldLayout = vk::ImageLayout::eTransferDstOptimal;
				barriers[1].newLayout = vk::ImageLayout::eColorAttachmentOptimal;
				commands.pipelineBarrier(
					vk::PipelineStageFlagBits::eTransfer,
					vk::PipelineStageFlagBits::eColorAttachmentOutput,
					{},
					{},
					{},
					barriers,
					**dispatcher
				);
			}
		}

		void operator()(execution::BeginRenderingCommand const& command) const {
			if (rendering) throw std::logic_error("Vulkan rendering commands cannot be nested");
			rendering = true;
			if (!dynamic_rendering) {
				pending_rendering = &command;
				BeginTraditionalRendering();
				return;
			}
			std::vector<vk::RenderingAttachmentInfo> colors;
			colors.reserve(command.colors.size());
			for (auto const& color : command.colors) {
				auto const& view = TextureView(color.view);
				vk::RenderingAttachmentInfo attachment;
				attachment.imageView = *view.impl;
				attachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
				attachment.loadOp = color.load ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear;
				attachment.storeOp = color.store ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare;
				attachment.clearValue.color = std::array{
					color.clear_red, color.clear_green, color.clear_blue, color.clear_alpha
				};
				if (color.resolve_view || color.resolve_resource) {
					if (!color.resolve_view || !color.resolve_resource) {
						throw std::invalid_argument("Vulkan resolve requires both a resource and a view");
					}
					auto const& resolve = TextureView(*color.resolve_view);
					attachment.resolveMode = vk::ResolveModeFlagBits::eAverage;
					attachment.resolveImageView = *resolve.impl;
					attachment.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
				}
				colors.emplace_back(attachment);
			}
			vk::RenderingAttachmentInfo depth;
			vk::RenderingAttachmentInfo* depth_pointer = nullptr;
			if (command.depth_stencil) {
				auto const& attachment = *command.depth_stencil;
				auto const& view = TextureView(attachment.view);
				depth.imageView = *view.impl;
				depth.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
				depth.loadOp = attachment.load_depth ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear;
				depth.storeOp = attachment.store_depth ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare;
				depth.clearValue.depthStencil = vk::ClearDepthStencilValue{
					attachment.clear_depth,
					attachment.clear_stencil
				};
				depth_pointer = &depth;
			}
			vk::RenderingInfo info;
			info.renderArea = vk::Rect2D{
				vk::Offset2D{ command.offset_x, command.offset_y },
				vk::Extent2D{ command.width, command.height }
			};
			info.layerCount = 1u;
			info.colorAttachmentCount = static_cast<std::uint32_t>(colors.size());
			info.pColorAttachments = colors.data();
			info.pDepthAttachment = depth_pointer;
			info.pStencilAttachment = depth_pointer;
			commands.beginRendering(info, **dispatcher);
		}

		void operator()(execution::EndRenderingCommand const&) const {
			if (!rendering) throw std::logic_error("Vulkan EndRendering has no matching BeginRendering");
			if (dynamic_rendering) {
				commands.endRendering(**dispatcher);
			}
			else {
				if (pending_rendering) {
					throw std::logic_error("Traditional Vulkan rendering requires a bound pipeline");
				}
				commands.endRenderPass(**dispatcher);
				ResolveTraditionalAttachments(*active_rendering);
			}
			rendering = false;
			pipeline = nullptr;
			active_rendering = nullptr;
		}

		void operator()(execution::BindPipelineCommand const& command) const {
			pipeline = &bindings->pipelines[command.pipeline.value].get();
			if (rendering && pipeline->bind_point != vk::PipelineBindPoint::eGraphics) {
				throw std::invalid_argument("Vulkan compute pipelines cannot be bound in a rendering scope");
			}
			BeginTraditionalRendering();
			commands.bindPipeline(pipeline->bind_point, *pipeline->impl, **dispatcher);
		}

		void operator()(execution::BindResourceGroupCommand const& command) const {
			if (!pipeline) {
				throw std::logic_error("Vulkan resource group requires a bound pipeline");
			}
			auto const& group = bindings->resource_groups[command.group.value].get();
			if (group.space != command.index) {
				throw std::invalid_argument("Vulkan resource group index does not match its pipeline space");
			}
			if (*group.layout != *pipeline->layout) {
				throw std::invalid_argument("Vulkan resource group was created for a different pipeline layout");
			}
			std::array descriptor_sets{ group.set };
			std::array<std::uint32_t, 0u> dynamic_offsets;
			commands.bindDescriptorSets(
				pipeline->bind_point,
				*pipeline->layout,
				group.space,
				descriptor_sets,
				dynamic_offsets,
				**dispatcher
			);
		}

		void operator()(execution::BindVertexBufferCommand const& command) const {
			auto const& resource = std::get<Backend::Resource::Buffer>(
				bindings->resources[command.resource.value].get().impl
			);
			std::array buffers{ vk::Buffer(resource.vk_handle) };
			std::array offsets{ static_cast<vk::DeviceSize>(command.offset) };
			commands.bindVertexBuffers(command.slot, buffers, offsets, **dispatcher);
		}

		void operator()(execution::BindIndexBufferCommand const& command) const {
			auto const& resource = std::get<Backend::Resource::Buffer>(
				bindings->resources[command.resource.value].get().impl
			);
			commands.bindIndexBuffer(
				resource.vk_handle,
				command.offset,
				command.uint32 ? vk::IndexType::eUint32 : vk::IndexType::eUint16,
				**dispatcher
			);
		}

		void operator()(execution::SetViewportCommand const& command) const {
			vk::Viewport viewport(
				command.x, command.y, command.width, command.height,
				command.minimum_depth, command.maximum_depth
			);
			commands.setViewport(0u, viewport, **dispatcher);
		}

		void operator()(execution::SetScissorCommand const& command) const {
			vk::Rect2D scissor(
				{ command.x, command.y },
				{ command.width, command.height }
			);
			commands.setScissor(0u, scissor, **dispatcher);
		}

		void operator()(execution::DrawCommand const& command) const {
			commands.draw(
				command.vertex_count, command.instance_count,
				command.first_vertex, command.first_instance,
				**dispatcher
			);
		}

		void operator()(execution::DrawIndexedCommand const& command) const {
			commands.drawIndexed(
				command.index_count, command.instance_count, command.first_index,
				command.vertex_offset, command.first_instance,
				**dispatcher
			);
		}

		void operator()(execution::DispatchCommand const& command) const {
			if (!pipeline || pipeline->bind_point != vk::PipelineBindPoint::eCompute) {
				throw std::logic_error("Vulkan Dispatch requires a bound compute pipeline");
			}
			if (rendering) {
				throw std::logic_error("Vulkan Dispatch cannot execute in a rendering scope");
			}
			if (command.group_count_x == 0u || command.group_count_y == 0u || command.group_count_z == 0u) {
				throw std::invalid_argument("Vulkan Dispatch group counts must be non-zero");
			}
			commands.dispatch(
				command.group_count_x,
				command.group_count_y,
				command.group_count_z,
				**dispatcher
			);
		}

		void operator()(execution::CopyBufferCommand const& command) const {
			auto const& source = std::get<Backend::Resource::Buffer>(
				bindings->resources[command.source.value].get().impl
			);
			auto const& destination = std::get<Backend::Resource::Buffer>(
				bindings->resources[command.destination.value].get().impl
			);
			vk::BufferCopy region(command.source_offset, command.destination_offset, command.size);
			commands.copyBuffer(source.vk_handle, destination.vk_handle, region, **dispatcher);
		}

		void operator()(execution::PresentCommand const& command) const {
			if (rendering) {
				throw std::logic_error("Vulkan Present cannot execute in a rendering scope");
			}
			auto const& resource = bindings->resources[command.source.value].get();
			auto source = std::get_if<Backend::Resource::Texture>(&resource.impl);
			if (!source || source->buf_info.imageType != VK_IMAGE_TYPE_2D ||
				source->buf_info.extent.depth != 1u || source->buf_info.arrayLayers != 1u ||
				source->buf_info.mipLevels != 1u || source->buf_info.samples != VK_SAMPLE_COUNT_1_BIT) {
				throw std::invalid_argument("Vulkan presentation source must be a single-sampled 2D texture");
			}
			if ((source->buf_info.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0u) {
				throw std::invalid_argument("Vulkan presentation source requires transfer-source usage");
			}
			presentations->push_back(
				{
					bindings->presentation_targets[command.target.value],
					command.source,
					source,
					command.vertical_sync,
					command.frames_in_flight
				}
			);
		}
	};

	struct VulkanCompletionPoll {
		std::vector<std::pair<std::shared_ptr<Backend::VulkanScheduler::QueueState>, std::uint64_t>> timelines;
		std::vector<std::pair<std::shared_ptr<Backend::VulkanScheduler::QueueState>, vk::SharedFence>> fences;

		bool operator()() const {
			for (auto const& timeline : timelines) {
				auto const& synchronization = std::get<
					Backend::VulkanScheduler::TimelineSynchronization
				>(timeline.first->synchronization);
				if (timeline.first->device->getSemaphoreCounterValue(
					*synchronization.semaphore,
					*timeline.first->dispatcher
				) < timeline.second) {
					return false;
				}
			}
			for (auto const& fence : fences) {
				if (fence.first->device->getFenceStatus(
					*fence.second,
					*fence.first->dispatcher
				) != vk::Result::eSuccess) {
					return false;
				}
			}
			return true;
		}
	};

	struct VulkanGraphCompletion {
		execution::GraphCompletion completion;
		std::vector<std::shared_ptr<Backend::VulkanScheduler::QueueState>> idle_queues;

		void operator()() const noexcept {
			try {
				for (auto const& queue : idle_queues) {
					std::unique_lock<std::mutex> lock(*queue->submission_mutex);
					queue->impl->waitIdle(*queue->dispatcher);
				}
				completion.SetValue(completion.operation);
			}
			catch (...) {
				auto error = std::current_exception();
				completion.SetError(completion.operation, error);
			}
		}
	};

}
namespace fyuu_rhi::vulkan {
	using namespace fyuu_rhi::pipeline;
	Backend::ExecutableGraph CompileCommandGraph(Backend::CommandGraph const& graph) {
		return execution::MakeExecutableGraph<Backend>(graph);
	}

	std::shared_ptr<Backend::VulkanScheduler::QueueState> const&
	Backend::VulkanScheduler::QueueCollection::Select(
		execution::GraphNodeFlagBits capability
	) const {
		using Flag = execution::GraphNodeFlagBits;
		if ((capability & (Flag::Graphics | Flag::Present)) != Flag::None && graphics) {
			return graphics;
		}
		if ((capability & Flag::Compute) != Flag::None && compute) {
			return compute;
		}
		if ((capability & Flag::Copy) != Flag::None && copy) {
			return copy;
		}
		throw std::invalid_argument("Command graph batch requires an unavailable Vulkan queue");
	}

	Backend::VulkanScheduler::BinarySynchronizationPool::SemaphorePool::Lease
	Backend::VulkanScheduler::BinarySynchronizationPool::AcquireSemaphore() {
		return semaphores->Acquire(CreateBinarySemaphore{ this });
	}

	Backend::VulkanScheduler::BinarySynchronizationPool::FencePool::Lease
	Backend::VulkanScheduler::BinarySynchronizationPool::AcquireFence() {
		return fences->Acquire(CreateBinaryFence{ this }, ResetBinaryFence{ this });
	}

	Backend::GraphExecution CreateGraphExecution(
		Backend::Scheduler const& scheduler,
		Backend::ExecutableGraph const& graph
	) {
		Backend::GraphExecution result{ scheduler, graph };
		auto const& native_graph = *graph->impl;
		std::vector<VulkanResourceState> states(native_graph.bindings.resources.size());
		auto const& last_users = graph->plan.last_resource_users;
		for (std::size_t index = 0u; index < native_graph.bindings.resources.size(); ++index) {
			auto const& resource = native_graph.bindings.resources[index].get();
			states[index].stages = vk::PipelineStageFlagBits2::eAllCommands;
			states[index].access = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
			if (auto texture = std::get_if<Backend::Resource::Texture>(&resource.impl)) {
				states[index].layout = texture->curr_layout.load(std::memory_order_acquire);
			}
		}
		bool synchronization2 = scheduler->impl->enabled_features.contains(
			vk::StructureType::ePhysicalDeviceSynchronization2Features
		);
		bool dynamic_rendering = scheduler->impl->enabled_features.contains(
			vk::StructureType::ePhysicalDeviceDynamicRenderingFeatures
		);
		result.batches.reserve(graph->plan.batches.size());
		for (auto const& plan : graph->plan.batches) {
			auto const& queue = scheduler->impl->queues.Select(plan.queue_flags);
			auto commands = queue->command_pool->Acquire(
				CreateVulkanCommandEntry{ queue.get() },
				ResetVulkanCommandEntry{ queue.get() }
			);
			auto command_buffer = *commands.Get().impl;
			std::vector<vk::SharedRenderPass> render_passes;
			std::vector<vk::SharedFramebuffer> framebuffers;
			std::vector<Backend::GraphExecution::Batch::PresentationRequest> presentations;
			VulkanCommandRecorder recorder{
				&native_graph.bindings,
				command_buffer,
				&queue->device,
				&queue->dispatcher,
				&render_passes,
				&framebuffers,
				&presentations,
				dynamic_rendering
			};
			for (auto node_id : plan.nodes) {
				for (auto const& barrier : plan.barriers) {
					if (barrier.destination.value != node_id.value) continue;
					auto const& source_queue = scheduler->impl->queues.Select(barrier.source_queue);
					auto const& destination_queue = scheduler->impl->queues.Select(barrier.destination_queue);
					if (source_queue == destination_queue ||
						source_queue->family == destination_queue->family) continue;
					auto source = GetVulkanResourceState(barrier.source_access);
					auto destination = GetVulkanResourceState(barrier.destination_access);
					source.stages = {};
					source.access = {};
					source.family = source_queue->family;
					destination.family = destination_queue->family;
					RecordVulkanBarrier(
						command_buffer,
						native_graph.bindings.resources[barrier.resource.value].get(),
						source,
						destination,
						barrier.destination_range,
						synchronization2,
						queue->dispatcher
					);
					states[barrier.resource.value] = destination;
				}
				auto const& node = native_graph.descriptor.nodes[node_id.value];
				for (auto const& access : node.accesses) {
					auto destination = GetVulkanResourceState(access.flags);
					destination.family = queue->family;
					auto const& source = states[access.resource.value];
					if (source.stages != destination.stages || source.access != destination.access ||
						source.layout != destination.layout || source.family != destination.family) {
						RecordVulkanBarrier(
							command_buffer,
							native_graph.bindings.resources[access.resource.value].get(),
							source,
							destination,
							access.range,
							synchronization2,
							queue->dispatcher
						);
						states[access.resource.value] = destination;
					}
				}
				for (auto const& command : node.commands) std::visit(recorder, command);
				for (auto const& barrier : plan.release_barriers) {
					if (barrier.source.value != node_id.value) continue;
					auto const& source_queue = scheduler->impl->queues.Select(barrier.source_queue);
					auto const& destination_queue = scheduler->impl->queues.Select(barrier.destination_queue);
					if (source_queue == destination_queue) continue;
					auto source = states[barrier.resource.value];
					auto destination = GetVulkanResourceState(barrier.destination_access);
					if (source_queue->family == destination_queue->family) {
						source.family = VK_QUEUE_FAMILY_IGNORED;
						destination.family = VK_QUEUE_FAMILY_IGNORED;
					}
					else {
						source.family = source_queue->family;
						destination.stages = {};
						destination.access = {};
						destination.family = destination_queue->family;
					}
					RecordVulkanBarrier(
						command_buffer,
						native_graph.bindings.resources[barrier.resource.value].get(),
						source,
						destination,
						barrier.source_range,
						synchronization2,
						queue->dispatcher
					);
					states[barrier.resource.value] = GetVulkanResourceState(barrier.destination_access);
					states[barrier.resource.value].family = destination_queue->family;
				}
				for (auto const& access : node.accesses) {
					if (last_users[access.resource.value].value != node_id.value) continue;
					auto final = VulkanResourceState{
						vk::PipelineStageFlagBits2::eAllCommands,
						vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite,
						vk::ImageLayout::eGeneral,
						queue->family
					};
					RecordVulkanBarrier(
						command_buffer,
						native_graph.bindings.resources[access.resource.value].get(),
						states[access.resource.value],
						final,
						access.range,
						synchronization2,
						queue->dispatcher
					);
					states[access.resource.value] = final;
					if (auto texture = std::get_if<Backend::Resource::Texture>(
						&native_graph.bindings.resources[access.resource.value].get().impl
					)) {
						texture->curr_layout.store(vk::ImageLayout::eGeneral, std::memory_order_release);
					}
				}
			}
			if (recorder.rendering) throw std::logic_error("Vulkan rendering scope must end in its batch");
			commands.Get().impl->end(*queue->dispatcher);
			commands.Get().recording = false;
			result.batches.emplace_back(
				queue,
				std::move(commands),
				std::move(render_passes),
				std::move(framebuffers),
				std::nullopt,
				0u
			);
			auto& batch = result.batches.back();
			batch.presentation_requests = std::move(presentations);
			if (!std::holds_alternative<
				Backend::VulkanScheduler::TimelineSynchronization
			>(queue->synchronization)) {
				auto const& pool = std::get<
					std::shared_ptr<Backend::VulkanScheduler::BinarySynchronizationPool>
				>(queue->synchronization);
				batch.fence.emplace(pool->AcquireFence());
			}
		}

		for (auto const& destination : graph->plan.batches) {
			for (auto source_id : destination.dependencies) {
				auto const& source = result.batches[source_id.value];
				auto const& target = result.batches[destination.id.value];
				if (source.queue == target.queue || std::holds_alternative<
					Backend::VulkanScheduler::TimelineSynchronization
				>(source.queue->synchronization)) {
					continue;
				}
				auto const& pool = std::get<
					std::shared_ptr<Backend::VulkanScheduler::BinarySynchronizationPool>
				>(source.queue->synchronization);
				result.binary_dependencies.emplace_back(
					source_id,
					destination.id,
					pool->AcquireSemaphore()
				);
			}
		}
		return result;
	}

	void StartGraphExecution(
		Backend::GraphExecution& graph_execution,
		execution::GraphCompletion const& completion
	) {
		auto const& plans = graph_execution.graph->plan.batches;
		for (std::size_t index = 0u; index < plans.size(); ++index) {
			auto& batch = graph_execution.batches[index];
			std::unique_lock<std::mutex> lock(*batch.queue->submission_mutex);
			std::vector<vk::Semaphore> wait_semaphores;
			std::vector<vk::PipelineStageFlags> wait_stages;
			std::vector<std::uint64_t> wait_values;
			std::vector<vk::Semaphore> signal_semaphores;
			std::vector<std::uint64_t> signal_values;

			if (auto synchronization = std::get_if<Backend::VulkanScheduler::TimelineSynchronization>(&batch.queue->synchronization)) {
				batch.synchronization_value = synchronization->next_value.fetch_add(
					1u,
					std::memory_order::relaxed
				);
				for (auto dependency : plans[index].dependencies) {
					auto const& source = graph_execution.batches[dependency.value];
					if (source.queue == batch.queue) {
						continue;
					}
					auto const& source_synchronization = std::get<
						Backend::VulkanScheduler::TimelineSynchronization
					>(source.queue->synchronization);
					wait_semaphores.emplace_back(*source_synchronization.semaphore);
					wait_stages.emplace_back(vk::PipelineStageFlagBits::eAllCommands);
					wait_values.emplace_back(source.synchronization_value);
				}
				signal_semaphores.emplace_back(*synchronization->semaphore);
				signal_values.emplace_back(batch.synchronization_value);
			}
			else {
				for (auto& dependency : graph_execution.binary_dependencies) {
					if (dependency.destination.value == index) {
						wait_semaphores.emplace_back(*dependency.semaphore.Get());
						wait_stages.emplace_back(vk::PipelineStageFlagBits::eAllCommands);
					}
					if (dependency.source.value == index) {
						signal_semaphores.emplace_back(*dependency.semaphore.Get());
					}
				}
			}

			vk::TimelineSemaphoreSubmitInfo timeline_info;
			timeline_info.waitSemaphoreValueCount = static_cast<std::uint32_t>(wait_values.size());
			timeline_info.pWaitSemaphoreValues = wait_values.data();
			bool has_presentations = !batch.presentation_requests.empty();
			timeline_info.signalSemaphoreValueCount = has_presentations ?
				0u : static_cast<std::uint32_t>(signal_values.size());
			timeline_info.pSignalSemaphoreValues = has_presentations ? nullptr : signal_values.data();
			auto command_buffer = *batch.commands.Get().impl;
			vk::SubmitInfo submit_info;
			submit_info.pNext = wait_values.empty() && signal_values.empty() ? nullptr : &timeline_info;
			submit_info.waitSemaphoreCount = static_cast<std::uint32_t>(wait_semaphores.size());
			submit_info.pWaitSemaphores = wait_semaphores.data();
			submit_info.pWaitDstStageMask = wait_stages.data();
			submit_info.commandBufferCount = 1u;
			submit_info.pCommandBuffers = &command_buffer;
			submit_info.signalSemaphoreCount = has_presentations ?
				0u : static_cast<std::uint32_t>(signal_semaphores.size());
			submit_info.pSignalSemaphores = has_presentations ? nullptr : signal_semaphores.data();
			vk::Fence fence;
			if (batch.fence && !has_presentations) {
				fence = *batch.fence->Get();
			}
			batch.queue->impl->submit(submit_info, fence, *batch.queue->dispatcher);

			for (std::size_t presentation_index = 0u;
				presentation_index < batch.presentation_requests.size();
				++presentation_index) {
				auto const& request = batch.presentation_requests[presentation_index];
				bool final_presentation =
					presentation_index + 1u == batch.presentation_requests.size();
				auto CreateEntry = [&]() {
					return CreatePresentationEntry(
						*graph_execution.scheduler->impl,
						*batch.queue,
						request.target,
						*request.source,
						request.vertical_sync,
						request.frames_in_flight
					);
				};
				auto presentation = graph_execution.scheduler->impl->presentation_cache->Acquire(
					request.target,
					CreateEntry
				);
				auto supported_modes = graph_execution.scheduler->impl->physical_device.impl
					->getSurfacePresentModesKHR(
						*presentation.Get().surface,
						*batch.queue->dispatcher
					);
				auto requested_mode = SelectPresentMode(
					supported_modes,
					request.vertical_sync,
					graph_execution.scheduler->impl->enabled_features.contains(
						vk::StructureType::ePhysicalDevicePresentModeFifoLatestReadyFeaturesKHR
					)
				);
				auto source_extent = vk::Extent2D(
					request.source->buf_info.extent.width,
					request.source->buf_info.extent.height
				);
				if (presentation.Get().format != static_cast<vk::Format>(request.source->buf_info.format) ||
					presentation.Get().extent != source_extent ||
					std::ranges::find(
						presentation.Get().compatible_present_modes,
						requested_mode
					) == presentation.Get().compatible_present_modes.end() ||
					presentation.Get().requested_frames_in_flight != request.frames_in_flight) {
					graph_execution.scheduler->impl->presentation_cache->Recreate(
						presentation,
						[&CreateEntry](Backend::LogicalDevice::PresentationEntry const&) {
							return CreateEntry();
						}
					);
					presentation = graph_execution.scheduler->impl->presentation_cache->Acquire(
						request.target,
						CreateEntry
					);
				}

				auto image_available = graph_execution.scheduler->impl->presentation_synchronization->AcquireSemaphore();
				std::uint32_t frame_index = 0u;
				auto AcquireImage = [&](Backend::LogicalDevice::PresentationEntry& entry) {
					std::unique_lock<std::mutex> lock(*entry.mutex);
					return batch.queue->device->acquireNextImageKHR(
						*entry.swapchain,
						(std::numeric_limits<std::uint64_t>::max)(),
						*image_available.Get(),
						nullptr,
						&frame_index,
						*batch.queue->dispatcher
					);
				};
				auto acquire_result = AcquireImage(presentation.Get());
				if (acquire_result == vk::Result::eErrorOutOfDateKHR) {
					batch.queue->impl->waitIdle(*batch.queue->dispatcher);
					graph_execution.scheduler->impl->presentation_cache->Recreate(
						presentation,
						[&CreateEntry](Backend::LogicalDevice::PresentationEntry const&) {
							return CreateEntry();
						}
					);
					presentation = graph_execution.scheduler->impl->presentation_cache->Acquire(
						request.target,
						CreateEntry
					);
					acquire_result = AcquireImage(presentation.Get());
				}
				if (acquire_result != vk::Result::eSuccess &&
					acquire_result != vk::Result::eSuboptimalKHR) {
					throw std::runtime_error(std::format(
						"Vulkan swapchain image acquisition failed: {}",
						static_cast<std::int32_t>(acquire_result)
					));
				}
				auto& presentation_entry = presentation.Get();
				std::unique_lock<std::mutex> presentation_lock(*presentation_entry.mutex);
				if (frame_index >= presentation_entry.frames.size()) {
					throw std::out_of_range("Vulkan swapchain returned an invalid image index");
				}
				auto& frame = presentation_entry.frames[frame_index];
				auto commands = batch.queue->command_pool->Acquire(
					CreateVulkanCommandEntry{ batch.queue.get() },
					ResetVulkanCommandEntry{ batch.queue.get() }
				);
				auto presentation_command_buffer = *commands.Get().impl;
				std::array barriers{
					vk::ImageMemoryBarrier(
						vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite,
						vk::AccessFlagBits::eTransferRead,
						vk::ImageLayout::eGeneral,
						vk::ImageLayout::eTransferSrcOptimal,
						VK_QUEUE_FAMILY_IGNORED,
						VK_QUEUE_FAMILY_IGNORED,
						request.source->vk_handle,
						vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u)
					),
					vk::ImageMemoryBarrier(
						{},
						vk::AccessFlagBits::eTransferWrite,
						frame.initialized ? vk::ImageLayout::ePresentSrcKHR : vk::ImageLayout::eUndefined,
						vk::ImageLayout::eTransferDstOptimal,
						VK_QUEUE_FAMILY_IGNORED,
						VK_QUEUE_FAMILY_IGNORED,
						frame.image,
						vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u)
					)
				};
				presentation_command_buffer.pipelineBarrier(
					vk::PipelineStageFlagBits::eAllCommands,
					vk::PipelineStageFlagBits::eTransfer,
					{}, {}, {}, barriers,
					*batch.queue->dispatcher
				);
				vk::ImageCopy region(
					vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0u, 0u, 1u),
					{},
					vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, 0u, 0u, 1u),
					{},
					vk::Extent3D(presentation_entry.extent.width, presentation_entry.extent.height, 1u)
				);
				presentation_command_buffer.copyImage(
					request.source->vk_handle,
					vk::ImageLayout::eTransferSrcOptimal,
					frame.image,
					vk::ImageLayout::eTransferDstOptimal,
					region,
					*batch.queue->dispatcher
				);
				std::array final_barriers{
					vk::ImageMemoryBarrier(
						vk::AccessFlagBits::eTransferRead,
						vk::AccessFlagBits::eMemoryRead | vk::AccessFlagBits::eMemoryWrite,
						vk::ImageLayout::eTransferSrcOptimal,
						vk::ImageLayout::eGeneral,
						VK_QUEUE_FAMILY_IGNORED,
						VK_QUEUE_FAMILY_IGNORED,
						request.source->vk_handle,
						vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u)
					),
					vk::ImageMemoryBarrier(
						vk::AccessFlagBits::eTransferWrite,
						{},
						vk::ImageLayout::eTransferDstOptimal,
						vk::ImageLayout::ePresentSrcKHR,
						VK_QUEUE_FAMILY_IGNORED,
						VK_QUEUE_FAMILY_IGNORED,
						frame.image,
						vk::ImageSubresourceRange(vk::ImageAspectFlagBits::eColor, 0u, 1u, 0u, 1u)
					)
				};
				presentation_command_buffer.pipelineBarrier(
					vk::PipelineStageFlagBits::eTransfer,
					vk::PipelineStageFlagBits::eAllCommands,
					{}, {}, {}, final_barriers,
					*batch.queue->dispatcher
				);
				commands.Get().impl->end(*batch.queue->dispatcher);
				commands.Get().recording = false;
				std::optional<
					Backend::VulkanScheduler::BinarySynchronizationPool::FencePool::Lease
				> present_fence;
				bool swapchain_maintenance = graph_execution.scheduler->impl->enabled_features.contains(
					vk::StructureType::ePhysicalDeviceSwapchainMaintenance1FeaturesKHR
				);
				if (swapchain_maintenance) {
					present_fence.emplace(
						graph_execution.scheduler->impl->presentation_synchronization->AcquireFence()
					);
				}
				auto wait_semaphore = *image_available.Get();
				std::vector<vk::Semaphore> presentation_signals{ *frame.render_finished };
				if (final_presentation) {
					presentation_signals.insert(
						presentation_signals.end(),
						signal_semaphores.begin(),
						signal_semaphores.end()
					);
				}
				auto wait_stage = vk::PipelineStageFlags(vk::PipelineStageFlagBits::eTransfer);
				std::vector<std::uint64_t> presentation_signal_values(
					presentation_signals.size(),
					0u
				);
				vk::TimelineSemaphoreSubmitInfo presentation_timeline_info;
				if (final_presentation && !signal_values.empty()) {
					std::ranges::copy(
						signal_values,
						presentation_signal_values.begin() + 1u
					);
					presentation_timeline_info.signalSemaphoreValueCount =
						static_cast<std::uint32_t>(presentation_signal_values.size());
					presentation_timeline_info.pSignalSemaphoreValues =
						presentation_signal_values.data();
				}
				vk::SubmitInfo presentation_submit;
				presentation_submit.pNext = presentation_timeline_info.signalSemaphoreValueCount == 0u ?
					nullptr : &presentation_timeline_info;
				presentation_submit.waitSemaphoreCount = 1u;
				presentation_submit.pWaitSemaphores = &wait_semaphore;
				presentation_submit.pWaitDstStageMask = &wait_stage;
				presentation_submit.commandBufferCount = 1u;
				presentation_submit.pCommandBuffers = &presentation_command_buffer;
				presentation_submit.signalSemaphoreCount =
					static_cast<std::uint32_t>(presentation_signals.size());
				presentation_submit.pSignalSemaphores = presentation_signals.data();
				vk::Fence presentation_submission_fence;
				if (final_presentation && batch.fence) {
					presentation_submission_fence = *batch.fence->Get();
				}
				batch.queue->impl->submit(
					presentation_submit,
					presentation_submission_fence,
					*batch.queue->dispatcher
				);
				vk::SwapchainKHR swapchain = *presentation_entry.swapchain;
				vk::Semaphore present_wait = *frame.render_finished;
				vk::PresentInfoKHR present_info;
				present_info.waitSemaphoreCount = 1u;
				present_info.pWaitSemaphores = &present_wait;
				present_info.swapchainCount = 1u;
				present_info.pSwapchains = &swapchain;
				present_info.pImageIndices = &frame_index;
				vk::SwapchainPresentModeInfoKHR present_mode_info;
				present_mode_info.swapchainCount = 1u;
				present_mode_info.pPresentModes = &requested_mode;
				vk::Fence native_present_fence = present_fence ?
					*present_fence->Get() : vk::Fence{};
				vk::SwapchainPresentFenceInfoKHR present_fence_info;
				present_fence_info.swapchainCount = 1u;
				present_fence_info.pFences = &native_present_fence;
				if (swapchain_maintenance) {
					present_mode_info.pNext = &present_fence_info;
					present_info.pNext = &present_mode_info;
				}
				auto present_result = batch.queue->impl->presentKHR(
					&present_info,
					*batch.queue->dispatcher
				);
				if (present_result != vk::Result::eSuccess &&
					present_result != vk::Result::eSuboptimalKHR &&
					present_result != vk::Result::eErrorOutOfDateKHR) {
					batch.queue->impl->waitIdle(*batch.queue->dispatcher);
					present_fence.reset();
					graph_execution.scheduler->impl->presentation_cache->Recreate(
						presentation,
						[&CreateEntry](Backend::LogicalDevice::PresentationEntry const&) {
							return CreateEntry();
						}
					);
					throw std::runtime_error(std::format("Vulkan presentation failed: {}",static_cast<std::int32_t>(present_result)));
				}
				frame.initialized = true;
				if (present_result == vk::Result::eSuboptimalKHR ||
					present_result == vk::Result::eErrorOutOfDateKHR) {
					if (present_fence && present_result == vk::Result::eSuboptimalKHR) {
						std::array fences{ *present_fence->Get() };
						auto wait_result = batch.queue->device->waitForFences(
							fences,
							true,
							(std::numeric_limits<std::uint64_t>::max)(),
							*batch.queue->dispatcher
						);
						if (wait_result != vk::Result::eSuccess) {
							throw std::runtime_error(
								std::format(
									"Waiting for Vulkan present fence failed: {}",
									static_cast<std::int32_t>(wait_result)
								)
							);
						}
					}
					else {
						batch.queue->impl->waitIdle(*batch.queue->dispatcher);
					}
					if (present_result == vk::Result::eErrorOutOfDateKHR) {
						present_fence.reset();
					}
					graph_execution.scheduler->impl->presentation_cache->Recreate(
						presentation,
						[&CreateEntry](Backend::LogicalDevice::PresentationEntry const&) {
							return CreateEntry();
						}
					);
				}
				batch.in_flight_presentations.push_back(
					{
						std::move(presentation),
						std::move(commands),
						std::move(image_available),
						std::move(present_fence)
					}
				);
			}
		}

		VulkanCompletionPoll poll;
		VulkanGraphCompletion graph_completion{ completion };
		for (auto const& batch : graph_execution.batches) {
			for (auto const& presentation : batch.in_flight_presentations) {
				if (presentation.present_fence) {
					poll.fences.emplace_back(batch.queue, presentation.present_fence->Get());
				}
				if (!presentation.present_fence && std::ranges::find(
					graph_completion.idle_queues,
					batch.queue
				) == graph_completion.idle_queues.end()) {
					graph_completion.idle_queues.push_back(batch.queue);
				}
			}
			if (batch.fence) {
				poll.fences.emplace_back(batch.queue, batch.fence->Get());
			}
			else {
				poll.timelines.emplace_back(batch.queue, batch.synchronization_value);
			}
		}
		graph_execution.scheduler->impl->completion_service->Enqueue(
			std::move(poll),
			std::move(graph_completion)
		);
	}

}
#endif // !defined(__APPLE__)
