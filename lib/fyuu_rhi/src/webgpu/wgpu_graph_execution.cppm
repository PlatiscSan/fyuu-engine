module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <atomic>
#include <concepts>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#include <webgpu/webgpu_cpp.h>
#endif // !defined(__APPLE__)
module fyuu_rhi:webgpu_graph_execution;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :webgpu_traits;

namespace fyuu_rhi::webgpu {
	namespace {
		using Bindings = execution::NativeCommandGraphBindings<Backend>;

		void CompleteWebGPUEventPolling() noexcept {

		}

		void EnqueueWebGPUFuture(
			Backend::Scheduler const& scheduler,
			wgpu::Future future
		) {
			auto PollFuture = [
				instance = scheduler->adapter.GetInstance(),
				future
			]() noexcept {
				return instance.WaitAny(future, 0u) != wgpu::WaitStatus::TimedOut;
			};
			scheduler->completion_service->Enqueue(
				std::move(PollFuture),
				CompleteWebGPUEventPolling
			);
		}

		wgpu::Surface CreateSurface(
			wgpu::Instance const& instance,
			Backend::PresentationTarget const& target
		) {
#if defined(_WIN32) || defined(__ANDROID__)
			return Backend::CreateSurface(instance, target);
#elif defined(__linux__)
			auto CreateSurface = [&instance](auto const& value) {
				if constexpr (std::same_as<
					std::remove_cvref_t<decltype(value)>,
					Backend::X11PresentationTarget
				>) {
					return Backend::CreateSurface(instance, value.display, value.window);
				}
				else {
					return Backend::CreateSurface(instance, value.display, value.surface);
				}
			};
			return std::visit(CreateSurface, target);
#endif
		}

		Backend::LogicalDevice::PresentationEntry CreatePresentationEntry(
			Backend::Scheduler const& scheduler,
			Backend::PresentationTarget const& target,
			wgpu::Texture const& source,
			bool vertical_sync,
			std::uint32_t frames_in_flight
		) {
			wgpu::InstanceDescriptor instance_descriptor{};
			auto instance = wgpu::CreateInstance(&instance_descriptor);
			auto surface = CreateSurface(instance, target);
			wgpu::SurfaceCapabilities capabilities;
			if (!surface.GetCapabilities(scheduler->adapter, &capabilities)) {
				throw std::runtime_error("WebGPU could not query presentation capabilities");
			}
			if (capabilities.formatCount == 0u || capabilities.presentModeCount == 0u) {
				throw std::runtime_error("WebGPU surface has no presentation configuration");
			}
			auto format = source.GetFormat();
			if (!std::ranges::contains(
				std::span(capabilities.formats, capabilities.formatCount), format
			)) {
				throw std::invalid_argument("WebGPU presentation source format is unsupported by the surface");
			}
			if ((capabilities.usages & wgpu::TextureUsage::CopyDst) == wgpu::TextureUsage::None) {
				throw std::invalid_argument("WebGPU surface does not support copy destination usage");
			}
			auto present_modes = std::span(capabilities.presentModes, capabilities.presentModeCount);
			auto present_mode = capabilities.presentModes[0];
			if (vertical_sync && std::ranges::contains(present_modes, wgpu::PresentMode::Fifo)) {
				present_mode = wgpu::PresentMode::Fifo;
			}
			else if (!vertical_sync && std::ranges::contains(present_modes, wgpu::PresentMode::Immediate)) {
				present_mode = wgpu::PresentMode::Immediate;
			}
			else if (!vertical_sync && std::ranges::contains(present_modes, wgpu::PresentMode::Mailbox)) {
				present_mode = wgpu::PresentMode::Mailbox;
			}
			wgpu::SurfaceConfiguration configuration{
				.device = scheduler->queues.graphics
					? scheduler->queues.graphics->device
					: scheduler->queues.copy->device,
				.format = format,
				.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopyDst,
				.width = source.GetWidth(),
				.height = source.GetHeight(),
				.presentMode = present_mode
			};
			surface.Configure(&configuration);
			auto frame_state = std::make_shared<Backend::LogicalDevice::PresentationEntry::FrameState>();
			frame_state->slots.reserve(frames_in_flight);
			for (std::uint32_t index = 0u; index < frames_in_flight; ++index) {
				frame_state->slots.emplace_back(
					std::make_shared<Backend::LogicalDevice::PresentationEntry::FrameSlot>(true)
				);
			}
			return {
				instance,
				surface,
				format,
				present_mode,
				source.GetWidth(),
				source.GetHeight(),
				vertical_sync,
				frames_in_flight,
				frame_state
			};
		}

		std::shared_ptr<Backend::LogicalDevice::PresentationEntry::FrameSlot> AcquireFrame(
			Backend::LogicalDevice::PresentationEntry& entry
		) {
			std::shared_ptr<Backend::LogicalDevice::PresentationEntry::FrameSlot> frame;
			{
				std::unique_lock<std::mutex> lock(entry.frames->mutex);
				frame = entry.frames->slots[entry.frames->next_slot];
				entry.frames->next_slot = (entry.frames->next_slot + 1u) % entry.frames->slots.size();
			}
			while (true) {
				bool expected = true;
				if (frame->compare_exchange_weak(
					expected,
					false,
					std::memory_order::acq_rel,
					std::memory_order::acquire
				)) {
					break;
				}
				frame->wait(false, std::memory_order::acquire);
			}
			return frame;
		}

		void ReleaseFrame(
			std::shared_ptr<Backend::LogicalDevice::PresentationEntry::FrameSlot> const& frame
		) noexcept {
			frame->store(true, std::memory_order::release);
			frame->notify_all();
		}

		struct WebGPUCommandRecorder {
			Bindings const* bindings;
			Backend::Scheduler const* scheduler;
			wgpu::CommandEncoder encoder;
			std::vector<Backend::GraphExecution::InFlightPresentation>* presentations;
			Backend::Pipeline const* current_pipeline = nullptr;
			wgpu::RenderPassEncoder render_pass;
			wgpu::ComputePassEncoder compute_pass;

			void EndComputePass() {
				if (compute_pass) {
					compute_pass.End();
					compute_pass = nullptr;
				}
			}

			void operator()(execution::BeginRenderingCommand const& command) {
				EndComputePass();
				if (render_pass) throw std::logic_error("WebGPU rendering commands cannot be nested");
				if (command.offset_x < 0 || command.offset_y < 0 ||
					command.width == 0u || command.height == 0u) {
					throw std::invalid_argument("WebGPU rendering area is invalid");
				}
				std::vector<wgpu::RenderPassColorAttachment> colors;
				colors.reserve(command.colors.size());
				for (auto const& color : command.colors) {
					wgpu::RenderPassColorAttachment attachment{
						.view = std::get<wgpu::TextureView>(bindings->views[color.view.value].get()),
						.resolveTarget = color.resolve_view
							? std::get<wgpu::TextureView>(bindings->views[color.resolve_view->value].get())
							: nullptr,
						.loadOp = color.load ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear,
						.storeOp = color.store ? wgpu::StoreOp::Store : wgpu::StoreOp::Discard,
						.clearValue = {
							color.clear_red, color.clear_green, color.clear_blue, color.clear_alpha
						}
					};
					colors.emplace_back(attachment);
				}
				std::optional<wgpu::RenderPassDepthStencilAttachment> depth_stencil;
				if (command.depth_stencil) {
					auto const& attachment = *command.depth_stencil;
					depth_stencil = {
						.view = std::get<wgpu::TextureView>(bindings->views[attachment.view.value].get()),
						.depthLoadOp = attachment.load_depth ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear,
						.depthStoreOp = attachment.store_depth ? wgpu::StoreOp::Store : wgpu::StoreOp::Discard,
						.depthClearValue = attachment.clear_depth,
						.stencilLoadOp = attachment.load_stencil ? wgpu::LoadOp::Load : wgpu::LoadOp::Clear,
						.stencilStoreOp = attachment.store_stencil ? wgpu::StoreOp::Store : wgpu::StoreOp::Discard,
						.stencilClearValue = attachment.clear_stencil
					};
				}
				wgpu::RenderPassDescriptor descriptor{
					.colorAttachmentCount = colors.size(),
					.colorAttachments = colors.data(),
					.depthStencilAttachment = depth_stencil ? &*depth_stencil : nullptr
				};
				render_pass = encoder.BeginRenderPass(&descriptor);
				render_pass.SetViewport(
					static_cast<float>(command.offset_x), static_cast<float>(command.offset_y),
					static_cast<float>(command.width), static_cast<float>(command.height),
					0.0f, 1.0f
				);
			}

			void operator()(execution::EndRenderingCommand const&) {
				if (!render_pass) throw std::logic_error("WebGPU rendering scope is not active");
				render_pass.End();
				render_pass = nullptr;
			}

			void operator()(execution::BindPipelineCommand const& command) {
				current_pipeline = &bindings->pipelines[command.pipeline.value].get();
				if (current_pipeline->compute) {
					if (render_pass) throw std::logic_error("WebGPU compute pipeline cannot be used in a render pass");
					if (!compute_pass) compute_pass = encoder.BeginComputePass();
					compute_pass.SetPipeline(std::get<wgpu::ComputePipeline>(current_pipeline->impl));
				}
				else {
					if (!render_pass) throw std::logic_error("WebGPU render pipeline requires a render pass");
					render_pass.SetPipeline(std::get<wgpu::RenderPipeline>(current_pipeline->impl));
				}
			}

			void operator()(execution::BindResourceGroupCommand const& command) {
				auto const& group = bindings->resource_groups[command.group.value].get();
				if (group.space != command.index) {
					throw std::invalid_argument("WebGPU resource group index does not match its pipeline space");
				}
				if (render_pass) render_pass.SetBindGroup(command.index, group.impl);
				else if (compute_pass) compute_pass.SetBindGroup(command.index, group.impl);
				else throw std::logic_error("WebGPU resource group requires an active pass");
			}

			void operator()(execution::BindVertexBufferCommand const& command) {
				if (!render_pass) throw std::logic_error("WebGPU vertex buffer requires a render pass");
				auto const& buffer = std::get<wgpu::Buffer>(bindings->resources[command.resource.value].get());
				render_pass.SetVertexBuffer(command.slot, buffer, command.offset, buffer.GetSize() - command.offset);
			}

			void operator()(execution::BindIndexBufferCommand const& command) {
				if (!render_pass) throw std::logic_error("WebGPU index buffer requires a render pass");
				auto const& buffer = std::get<wgpu::Buffer>(bindings->resources[command.resource.value].get());
				render_pass.SetIndexBuffer(
					buffer, command.uint32 ? wgpu::IndexFormat::Uint32 : wgpu::IndexFormat::Uint16,
					command.offset, buffer.GetSize() - command.offset
				);
			}

			void operator()(execution::SetViewportCommand const& command) {
				if (!render_pass) throw std::logic_error("WebGPU viewport requires a render pass");
				render_pass.SetViewport(
					command.x, command.y, command.width, command.height,
					command.minimum_depth, command.maximum_depth
				);
			}

			void operator()(execution::SetScissorCommand const& command) {
				if (!render_pass) throw std::logic_error("WebGPU scissor requires a render pass");
				if (command.x < 0 || command.y < 0) {
					throw std::invalid_argument("WebGPU scissor offset must not be negative");
				}
				render_pass.SetScissorRect(
					static_cast<std::uint32_t>(command.x), static_cast<std::uint32_t>(command.y),
					command.width, command.height
				);
			}

			void operator()(execution::DrawCommand const& command) {
				if (!render_pass || !current_pipeline || current_pipeline->compute) {
					throw std::logic_error("Invalid WebGPU draw state");
				}
				render_pass.Draw(
					command.vertex_count, command.instance_count,
					command.first_vertex, command.first_instance
				);
			}

			void operator()(execution::DrawIndexedCommand const& command) {
				if (!render_pass || !current_pipeline || current_pipeline->compute) {
					throw std::logic_error("Invalid WebGPU indexed draw state");
				}
				render_pass.DrawIndexed(
					command.index_count, command.instance_count, command.first_index,
					command.vertex_offset, command.first_instance
				);
			}

			void operator()(execution::DispatchCommand const& command) {
				if (!compute_pass || !current_pipeline || !current_pipeline->compute) {
					throw std::logic_error("Invalid WebGPU dispatch state");
				}
				compute_pass.DispatchWorkgroups(
					command.group_count_x, command.group_count_y, command.group_count_z
				);
			}

			void operator()(execution::CopyBufferCommand const& command) {
				if (render_pass) throw std::logic_error("WebGPU copy cannot execute in a render pass");
				EndComputePass();
				auto const& source = std::get<wgpu::Buffer>(bindings->resources[command.source.value].get());
				auto const& destination = std::get<wgpu::Buffer>(bindings->resources[command.destination.value].get());
				if (command.source_offset + command.size > source.GetSize() ||
					command.destination_offset + command.size > destination.GetSize()) {
					throw std::out_of_range("WebGPU buffer copy exceeds the resource range");
				}
				encoder.CopyBufferToBuffer(
					source, command.source_offset, destination, command.destination_offset, command.size
				);
			}

			void operator()(execution::CopyBufferToTextureCommand const& command) {
				if (render_pass) throw std::logic_error("WebGPU copy cannot execute in a render pass");
				EndComputePass();
				auto const& source = std::get<wgpu::Buffer>(bindings->resources[command.source.value].get());
				auto const& destination = std::get<wgpu::Texture>(bindings->resources[command.destination.value].get());
				auto const& region = command.destination_region;
				wgpu::TexelCopyBufferInfo source_copy{
					.layout = {
						.offset = command.source_layout.offset,
						.bytesPerRow = command.source_layout.bytes_per_row,
						.rowsPerImage = command.source_layout.rows_per_image
					},
					.buffer = source
				};
				bool texture_3d = destination.GetDimension() == wgpu::TextureDimension::e3D;
				wgpu::TexelCopyTextureInfo destination_copy{
					.texture = destination,
					.mipLevel = region.mip_level,
					.origin = { region.offset_x, region.offset_y,
						texture_3d ? region.offset_z : region.base_array_layer },
					.aspect = wgpu::TextureAspect::All
				};
				wgpu::Extent3D extent{
					region.width, region.height,
					texture_3d ? region.depth : region.array_layer_count
				};
				encoder.CopyBufferToTexture(&source_copy, &destination_copy, &extent);
			}

			void operator()(execution::CopyTextureToBufferCommand const& command) {
				if (render_pass) throw std::logic_error("WebGPU copy cannot execute in a render pass");
				EndComputePass();
				auto const& source = std::get<wgpu::Texture>(bindings->resources[command.source.value].get());
				auto const& destination = std::get<wgpu::Buffer>(bindings->resources[command.destination.value].get());
				auto const& region = command.source_region;
				bool texture_3d = source.GetDimension() == wgpu::TextureDimension::e3D;
				wgpu::TexelCopyTextureInfo source_copy{
					.texture = source,
					.mipLevel = region.mip_level,
					.origin = { region.offset_x, region.offset_y,
						texture_3d ? region.offset_z : region.base_array_layer },
					.aspect = wgpu::TextureAspect::All
				};
				wgpu::TexelCopyBufferInfo destination_copy{
					.layout = {
						.offset = command.destination_layout.offset,
						.bytesPerRow = command.destination_layout.bytes_per_row,
						.rowsPerImage = command.destination_layout.rows_per_image
					},
					.buffer = destination
				};
				wgpu::Extent3D extent{ region.width, region.height,
					texture_3d ? region.depth : region.array_layer_count };
				encoder.CopyTextureToBuffer(&source_copy, &destination_copy, &extent);
			}

			void operator()(execution::CopyTextureCommand const& command) {
				if (render_pass) throw std::logic_error("WebGPU copy cannot execute in a render pass");
				EndComputePass();
				auto const& source = std::get<wgpu::Texture>(bindings->resources[command.source.value].get());
				auto const& destination = std::get<wgpu::Texture>(bindings->resources[command.destination.value].get());
				auto const& source_region = command.source_region;
				auto const& destination_region = command.destination_region;
				bool source_3d = source.GetDimension() == wgpu::TextureDimension::e3D;
				bool destination_3d = destination.GetDimension() == wgpu::TextureDimension::e3D;
				wgpu::TexelCopyTextureInfo source_copy{
					.texture = source,
					.mipLevel = source_region.mip_level,
					.origin = { source_region.offset_x, source_region.offset_y,
						source_3d ? source_region.offset_z : source_region.base_array_layer },
					.aspect = wgpu::TextureAspect::All
				};
				wgpu::TexelCopyTextureInfo destination_copy{
					.texture = destination,
					.mipLevel = destination_region.mip_level,
					.origin = { destination_region.offset_x, destination_region.offset_y,
						destination_3d ? destination_region.offset_z : destination_region.base_array_layer },
					.aspect = wgpu::TextureAspect::All
				};
				wgpu::Extent3D extent{ source_region.width, source_region.height,
					source_3d ? source_region.depth : source_region.array_layer_count };
				encoder.CopyTextureToTexture(&source_copy, &destination_copy, &extent);
			}

			void operator()(execution::PresentCommand const& command) {
				if (render_pass) throw std::logic_error("WebGPU present cannot execute in a render pass");
				EndComputePass();
				auto const& source = std::get<wgpu::Texture>(bindings->resources[command.source.value].get());
				auto const& target = bindings->presentation_targets[command.target.value];
				auto presentation = (*scheduler)->presentation_cache->Acquire(
					target,
					CreatePresentationEntry,
					*scheduler,
					target,
					source,
					command.vertical_sync,
					command.frames_in_flight
				);
				if (presentation.Get().width != source.GetWidth() ||
					presentation.Get().height != source.GetHeight() ||
					presentation.Get().format != source.GetFormat() ||
					presentation.Get().frames_in_flight != command.frames_in_flight ||
					presentation.Get().vertical_sync != command.vertical_sync) {
					(*scheduler)->presentation_cache->Recreate(
						presentation,
						CreatePresentationEntry,
						*scheduler,
						target,
						source,
						command.vertical_sync,
						command.frames_in_flight
					);
					presentation = (*scheduler)->presentation_cache->Acquire(
						target,
						CreatePresentationEntry,
						*scheduler,
						target,
						source,
						command.vertical_sync,
						command.frames_in_flight
					);
				}
				auto frame = AcquireFrame(presentation.Get());
				wgpu::SurfaceTexture surface_texture;
				presentation.Get().surface.GetCurrentTexture(&surface_texture);
				if (surface_texture.status == wgpu::SurfaceGetCurrentTextureStatus::Outdated ||
					surface_texture.status == wgpu::SurfaceGetCurrentTextureStatus::Lost) {
					ReleaseFrame(frame);
					(*scheduler)->presentation_cache->Recreate(
						presentation,
						CreatePresentationEntry,
						*scheduler,
						target,
						source,
						command.vertical_sync,
						command.frames_in_flight
					);
					presentation = (*scheduler)->presentation_cache->Acquire(
						target,
						CreatePresentationEntry,
						*scheduler,
						target,
						source,
						command.vertical_sync,
						command.frames_in_flight
					);
					frame = AcquireFrame(presentation.Get());
					presentation.Get().surface.GetCurrentTexture(&surface_texture);
				}
				if (surface_texture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
					surface_texture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal) {
					ReleaseFrame(frame);
					throw std::runtime_error("WebGPU could not acquire the current surface texture");
				}
				wgpu::TexelCopyTextureInfo source_copy{ .texture = source };
				wgpu::TexelCopyTextureInfo destination_copy{ .texture = surface_texture.texture };
				wgpu::Extent3D size{ source.GetWidth(), source.GetHeight(), 1u };
				encoder.CopyTextureToTexture(&source_copy, &destination_copy, &size);
				presentations->push_back({ std::move(presentation), frame });
			}
		};
	}

	Backend::ExecutableGraph Backend::CompileCommandGraph(Backend::CommandGraph const& graph) {
		return execution::MakeExecutableGraph<Backend>(graph);
	}

	Backend::GraphExecution CreateGraphExecution(
		Backend::Scheduler const& scheduler,
		Backend::ExecutableGraph const& graph
	) {
		Backend::GraphExecution result{ scheduler, graph };
		auto const& native_graph = *graph->impl;
		result.batches.reserve(graph->plan.batches.size());
		for (auto const& batch_plan : graph->plan.batches) {
			auto const& queue = scheduler->queues.Select(batch_plan.queue_flags);
			auto encoder = queue->device.CreateCommandEncoder();
			std::vector<Backend::GraphExecution::InFlightPresentation> presentations;
			WebGPUCommandRecorder recorder{ &native_graph.bindings, &scheduler, encoder, &presentations };
			for (auto node_id : batch_plan.nodes) {
				for (auto const& command : native_graph.descriptor.nodes[node_id.value].commands) {
					std::visit(recorder, command);
				}
			}
			if (recorder.render_pass) throw std::logic_error("WebGPU rendering scope must end in its batch");
			recorder.EndComputePass();
			result.batches.emplace_back(queue, encoder.Finish(), std::move(presentations));
		}
		return result;
	}

	void StartGraphExecution(
		Backend::GraphExecution& graph_execution,
		execution::GraphCompletion const& completion
	) {
		wgpu::Queue completion_queue;
		for (auto& batch : graph_execution.batches) {
			batch.queue->impl.Submit(1u, &batch.commands);
			completion_queue = batch.queue->impl;
			for (auto& presentation : batch.presentations) {
				presentation.entry.Get().surface.Present();
				auto CompleteFrame = [frame = presentation.frame](
					wgpu::QueueWorkDoneStatus,
					char const*
				) noexcept {
					frame->store(true, std::memory_order_release);
				};
				auto future = batch.queue->impl.OnSubmittedWorkDone(
					wgpu::CallbackMode::WaitAnyOnly,
					CompleteFrame
				);
				EnqueueWebGPUFuture(graph_execution.scheduler, future);
			}
		}
		if (!completion_queue) {
			completion_queue = graph_execution.scheduler->queues.graphics
				? graph_execution.scheduler->queues.graphics->impl
				: graph_execution.scheduler->queues.compute
					? graph_execution.scheduler->queues.compute->impl
					: graph_execution.scheduler->queues.copy->impl;
		}
		auto CompleteGraph = [completion](wgpu::QueueWorkDoneStatus status, char const* message) noexcept {
			if (status == wgpu::QueueWorkDoneStatus::Success) {
				completion.SetValue(completion.operation);
			}
			else {
				try {
					throw std::runtime_error(message ? message : "WebGPU queue execution failed");
				}
				catch (...) {
					auto error = std::current_exception();
					completion.SetError(completion.operation, error);
				}
			}
		};
		auto future = completion_queue.OnSubmittedWorkDone(
			wgpu::CallbackMode::WaitAnyOnly,
			CompleteGraph
		);
		EnqueueWebGPUFuture(graph_execution.scheduler, future);
	}

	void StartSchedulerExecution(
		Backend::Scheduler const& scheduler,
		execution::SchedulerCompletion const& completion
	) {
		auto const& queue_state = scheduler->queues.graphics
			? scheduler->queues.graphics
			: scheduler->queues.compute
				? scheduler->queues.compute
				: scheduler->queues.copy;
		if (!queue_state) {
			throw std::logic_error("WebGPU scheduler has no execution queue");
		}
		auto CompleteSchedule = [completion, scheduler](
			wgpu::QueueWorkDoneStatus status,
			char const* message
		) noexcept {
			(void)scheduler;
			if (status == wgpu::QueueWorkDoneStatus::Success) {
				completion.SetValue(completion.operation);
				return;
			}
			try {
				throw std::runtime_error(message ? message : "WebGPU scheduler execution failed");
			}
			catch (...) {
				auto error = std::current_exception();
				completion.SetError(completion.operation, error);
			}
		};
		auto future = queue_state->impl.OnSubmittedWorkDone(
			wgpu::CallbackMode::WaitAnyOnly,
			CompleteSchedule
		);
		EnqueueWebGPUFuture(scheduler, future);
	}


	std::shared_ptr<Backend::WebGPUScheduler::QueueState> const&
	Backend::WebGPUScheduler::QueueCollection::Select(
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
		throw std::invalid_argument("Command graph batch requires an unavailable WebGPU queue");
	}

	void StartDeferredDestroy(
		Backend::Scheduler const&,
		execution::DeferredDestroy const& deferred_destroy
	) {
		deferred_destroy.Destroy(deferred_destroy.object);
		deferred_destroy.completion.SetValue(deferred_destroy.completion.operation);
	}

	void StartMapResource(
		Backend::Scheduler const& scheduler,
		Backend::Resource& resource,
		execution::ResourceMapRequest const& request
	) {
		auto* buffer = std::get_if<wgpu::Buffer>(&resource);
		if (!buffer) {
			throw std::invalid_argument("WebGPU texture resources cannot be mapped directly");
		}
		wgpu::MapMode mode = wgpu::MapMode::None;
		if (request.read) mode |= wgpu::MapMode::Read;
		if (request.write) mode |= wgpu::MapMode::Write;
		auto completed = std::make_shared<std::atomic_bool>(false);
		auto CompleteMap = [buffer = *buffer, request, completed](
			wgpu::MapAsyncStatus status,
			wgpu::StringView message
		) noexcept {
			if (completed->exchange(true, std::memory_order::acq_rel)) {
				return;
			}
			if (status != wgpu::MapAsyncStatus::Success) {
				std::exception_ptr error;
				try {
					throw std::runtime_error(
						std::string(message.data, message.length)
					);
				}
				catch (...) {
					error = std::current_exception();
				}
				request.completion.SetError(request.completion.operation, error);
				return;
			}
			void* mapped = request.write
				? buffer.GetMappedRange(request.offset, request.size)
				: const_cast<void*>(buffer.GetConstMappedRange(request.offset, request.size));
			if (!mapped) {
				std::exception_ptr error;
				try {
					throw std::runtime_error("WebGPU returned an empty mapped range");
				}
				catch (...) {
					error = std::current_exception();
				}
				request.completion.SetError(request.completion.operation, error);
				return;
			}
			request.completion.SetValue(
				request.completion.operation,
				static_cast<std::byte*>(mapped)
			);
		};
		auto future = buffer->MapAsync(
			mode,
			request.offset,
			request.size,
			wgpu::CallbackMode::WaitAnyOnly,
			CompleteMap
		);
		auto PollMap = [
			instance = scheduler->adapter.GetInstance(),
			future,
			request,
			completed
		]() noexcept {
			auto status = instance.WaitAny(future, 0u);
			if (status == wgpu::WaitStatus::TimedOut) {
				return false;
			}
			if (status == wgpu::WaitStatus::Success) {
				return completed->load(std::memory_order_acquire);
			}
			if (!completed->exchange(true, std::memory_order::acq_rel)) {
				std::exception_ptr error;
				try {
					throw std::runtime_error("WebGPU MapAsync WaitAny failed");
				}
				catch (...) {
					error = std::current_exception();
				}
				request.completion.SetError(request.completion.operation, error);
			}
			return true;
		};
		scheduler->completion_service->Enqueue(
			std::move(PollMap),
			CompleteWebGPUEventPolling
		);
	}

	void StartUnmapResource(
		Backend::Scheduler const&,
		Backend::Resource& resource,
		execution::ResourceUnmapRequest const& request
	) {
		auto* buffer = std::get_if<wgpu::Buffer>(&resource);
		if (!buffer) {
			throw std::invalid_argument("WebGPU texture resources cannot be unmapped directly");
		}
		buffer->Unmap();
		request.completion.SetValue(request.completion.operation);
	}

}
#endif // !defined(__APPLE__)
