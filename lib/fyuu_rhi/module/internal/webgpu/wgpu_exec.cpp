module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstring>

#include <exception>
#include <stdexcept>

#include <algorithm>

#include <memory>

#include <vector>

#include <string>

#include <cstdint>
#include <utility>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <optional>
#include <variant>

#include <span>

#include <format>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <wayland-client-core.h>
#include <wayland-util.h>
#elif defined(__ANDROID__)
#include <android/native_window.h>
#include <android/android_native_app_glue.h>
#endif // defined(_WIN32)
#include <webgpu/webgpu_cpp.h>
module fyuu_rhi:webgpu_execution;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :webgpu_traits;
import :execution_types;
import :pipeline_types;

namespace {

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::execution;
	using namespace fyuu_rhi::pipeline;
	using fyuu_rhi::webgpu::Backend;

	wgpu::LoadOp NativeLoadOp(LoadOperation op) {
		switch (op) {
		case LoadOperation::Load: return wgpu::LoadOp::Load;
		case LoadOperation::Clear: return wgpu::LoadOp::Clear;
		case LoadOperation::Discard: return wgpu::LoadOp::Clear;   // no discard loadOp in WebGPU
		}
		return wgpu::LoadOp::Load;
	}

	wgpu::StoreOp NativeStoreOp(StoreOperation op) {
		switch (op) {
		case StoreOperation::Store: return wgpu::StoreOp::Store;
		case StoreOperation::Discard: return wgpu::StoreOp::Discard;
		}
		return wgpu::StoreOp::Store;
	}

	wgpu::IndexFormat NativeIndexFormat(IndexType type) {
		return type == IndexType::Uint16 ? wgpu::IndexFormat::Uint16 : wgpu::IndexFormat::Uint32;
	}

	/// Unpacks the platform handle and creates the surface through the instance.
	wgpu::Surface CreateSurfaceFor(
		wgpu::Instance const& instance,
		Backend::PlatformHandle const& handle
	) {
#if defined(_WIN32)
		wgpu::SurfaceSourceWindowsHWND win32_desc{};
		win32_desc.hinstance = GetModuleHandle(nullptr);
		win32_desc.hwnd = handle;
		wgpu::SurfaceDescriptor surface_desc{};
		surface_desc.nextInChain = &win32_desc;
		return instance.CreateSurface(&surface_desc);
#elif defined(__ANDROID__)
		wgpu::SurfaceDescriptorFromAndroidNativeWindow android_desc{};
		android_desc.window = handle;
		wgpu::SurfaceDescriptor surface_desc{};
		surface_desc.nextInChain = &android_desc;
		return instance.CreateSurface(&surface_desc);
#elif defined(__linux__)
		return std::visit(
			[&](auto const& native) -> wgpu::Surface {
				using T = std::remove_cvref_t<decltype(native)>;
				if constexpr (std::same_as<T, Backend::X11PlatformHandle>) {
					wgpu::SurfaceDescriptorFromXlibWindow xlib_desc{};
					xlib_desc.display = native.display;
					xlib_desc.window = native.window;
					wgpu::SurfaceDescriptor surface_desc{};
					surface_desc.nextInChain = &xlib_desc;
					return instance.CreateSurface(&surface_desc);
				}
				else {
					wgpu::SurfaceDescriptorFromWaylandSurface wayland_desc{};
					wayland_desc.display = native.display;
					wayland_desc.surface = native.surface;
					wgpu::SurfaceDescriptor surface_desc{};
					surface_desc.nextInChain = &wayland_desc;
					return instance.CreateSurface(&surface_desc);
				}
			},
			handle
		);
#endif
	}

} // namespace

namespace fyuu_rhi::webgpu {

	struct Backend::CompletionToken::Implementation {
		/// Needed by Poll to drive callbacks; a handle copy, not shared ownership.
		wgpu::Instance instance;
		std::atomic<bool> complete = false;
		std::atomic<bool> stopped = false;
		std::exception_ptr error;
	};

	void Backend::CompletionToken::Deleter::operator()(Implementation* impl) const noexcept {
		// The completion callback holds a raw pointer and fires before the token is
		// destroyed (the front end only destroys a token after Poll returns true).
		delete impl;
	}

	Backend::CompletionToken::~CompletionToken() noexcept = default;

	/// Acquired presentation state prepared in phase 1; consumed in order by
	/// Present commands during encoding.
	struct PresentationWork {
		std::size_t source;
		wgpu::Surface surface;
		wgpu::Texture current_texture;
	};

	/// Encodes a batch's commands into a WebGPU command encoder. WebGPU
	/// auto-synchronizes, so the plan's barrier/access machinery is ignored.
	struct Replayer {
		std::span<std::reference_wrapper<Backend::Resource> const> resources;
		std::span<std::reference_wrapper<Backend::View> const> views;
		std::span<std::reference_wrapper<Backend::Pipeline> const> pipelines;
		std::span<std::reference_wrapper<Backend::PipelineResourceGroup> const> groups;
		std::vector<PresentationWork> const& presentations;
		wgpu::Instance instance;
		std::size_t presentation_cursor = 0u;

		wgpu::CommandEncoder encoder;
		wgpu::RenderPassEncoder render_pass;
		wgpu::ComputePassEncoder compute_pass;
		wgpu::ComputePipeline compute_pipeline;

		// WebGPU requires all graphics state to be set inside the render pass, but
		// the plan may record Bind*/Viewport/Scissor before BeginRendering (D3D12 and
		// Vulkan accept that). Defer such state and flush it into the pass on Begin.

		struct VertexBufferPending {
			std::uint32_t slot;
			wgpu::Buffer buffer;
			std::uint64_t offset;
		};
		struct IndexBufferPending {
			wgpu::Buffer buffer;
			wgpu::IndexFormat format;
			std::uint64_t offset;
		};
		struct ViewportPending {
			float x;
			float y;
			float width;
			float height;
			float min_depth;
			float max_depth;
		};
		struct ScissorPending {
			std::uint32_t x;
			std::uint32_t y;
			std::uint32_t width;
			std::uint32_t height;
		};
		struct BindGroupPending {
			std::uint32_t index;
			wgpu::BindGroup group;
		};

		wgpu::RenderPipeline pending_pipeline;
		std::vector<VertexBufferPending> pending_vertex_buffers;
		std::optional<IndexBufferPending> pending_index_buffer;
		std::optional<ViewportPending> pending_viewport;
		std::optional<ScissorPending> pending_scissor;
		std::vector<BindGroupPending> pending_bind_groups;

		void FlushRenderState() {
			if (pending_pipeline) {
				render_pass.SetPipeline(pending_pipeline);
				pending_pipeline = nullptr;
			}
			for (auto const& vertex : pending_vertex_buffers) {
				render_pass.SetVertexBuffer(vertex.slot, vertex.buffer, vertex.offset, wgpu::kWholeSize);
			}
			pending_vertex_buffers.clear();
			if (pending_index_buffer) {
				render_pass.SetIndexBuffer(
					pending_index_buffer->buffer,
					pending_index_buffer->format,
					pending_index_buffer->offset,
					wgpu::kWholeSize
				);
				pending_index_buffer.reset();
			}
			if (pending_viewport) {
				render_pass.SetViewport(
					pending_viewport->x,
					pending_viewport->y,
					pending_viewport->width,
					pending_viewport->height,
					pending_viewport->min_depth,
					pending_viewport->max_depth
				);
				pending_viewport.reset();
			}
			if (pending_scissor) {
				render_pass.SetScissorRect(
					pending_scissor->x,
					pending_scissor->y,
					pending_scissor->width,
					pending_scissor->height
				);
				pending_scissor.reset();
			}
			for (auto const& group : pending_bind_groups) {
				render_pass.SetBindGroup(group.index, group.group);
			}
			pending_bind_groups.clear();
		}

		/// Ends any active pass. Encoder-level commands (copies, presents) are
		/// invalid inside a pass.
		void EndPass() {
			if (render_pass) {
				render_pass.End();
				render_pass = nullptr;
			}
			if (compute_pass) {
				compute_pass.End();
				compute_pass = nullptr;
			}
		}

		wgpu::TextureView TextureViewAt(std::size_t index) const {
			// Backend::View is itself the variant.
			auto const* view = std::get_if<wgpu::TextureView>(&views[index].get());
			if (!view) {
				throw std::invalid_argument("WebGPU command requires a texture view");
			}
			return *view;
		}

		wgpu::Texture TextureAt(std::size_t index) const {
			auto const* texture = std::get_if<wgpu::Texture>(&resources[index].get());
			if (!texture) {
				throw std::invalid_argument("WebGPU command requires a texture resource");
			}
			return *texture;
		}

		wgpu::Buffer BufferAt(std::size_t index) const {
			auto const* buffer = std::get_if<wgpu::Buffer>(&resources[index].get());
			if (!buffer) {
				throw std::invalid_argument("WebGPU command requires a buffer resource");
			}
			return *buffer;
		}

		void operator()(BeginRendering const& value) {
			EndPass();
			std::vector<wgpu::RenderPassColorAttachment> colors;
			colors.reserve(value.colors.size());
			for (auto const& color : value.colors) {
				wgpu::RenderPassColorAttachment attachment;
				attachment.view = TextureViewAt(color.view);
				attachment.depthSlice = wgpu::kDepthSliceUndefined;
				attachment.loadOp = NativeLoadOp(color.load);
				attachment.storeOp = NativeStoreOp(color.store);
				if (color.load == LoadOperation::Clear) {
					attachment.clearValue = wgpu::Color{ color.clear.red, color.clear.green, color.clear.blue, color.clear.alpha };
				}
				if (color.resolve_view) {
					attachment.resolveTarget = TextureViewAt(*color.resolve_view);
				}
				colors.emplace_back(attachment);
			}
			std::optional<wgpu::RenderPassDepthStencilAttachment> depth;
			if (value.depth_stencil) {
				auto const& attachment = *value.depth_stencil;
				wgpu::RenderPassDepthStencilAttachment ds;
				ds.view = TextureViewAt(attachment.view);
				ds.depthLoadOp = NativeLoadOp(attachment.depth_load);
				ds.depthStoreOp = NativeStoreOp(attachment.depth_store);
				ds.stencilLoadOp = NativeLoadOp(attachment.stencil_load);
				ds.stencilStoreOp = NativeStoreOp(attachment.stencil_store);
				ds.depthClearValue = attachment.clear_depth;
				ds.stencilClearValue = attachment.clear_stencil;
				depth = ds;
			}
			wgpu::RenderPassDescriptor descriptor;
			descriptor.colorAttachmentCount = static_cast<std::uint32_t>(colors.size());
			descriptor.colorAttachments = colors.data();
			descriptor.depthStencilAttachment = depth ? &*depth : nullptr;
			render_pass = encoder.BeginRenderPass(&descriptor);
			render_pass.SetViewport(
				static_cast<float>(value.area.x),
				static_cast<float>(value.area.y),
				static_cast<float>(value.area.width),
				static_cast<float>(value.area.height),
				0.0f,
				1.0f
			);
			render_pass.SetScissorRect(
				static_cast<std::uint32_t>(value.area.x),
				static_cast<std::uint32_t>(value.area.y),
				static_cast<std::uint32_t>(value.area.width),
				static_cast<std::uint32_t>(value.area.height)
			);
			// Deferred Bind*/Viewport/Scissor state recorded before this pass now
			// takes effect; it overrides the area-derived viewport/scissor above.
			FlushRenderState();
		}

		void operator()(EndRendering const&) {
			if (!render_pass) {
				throw std::logic_error("WebGPU EndRendering without BeginRendering");
			}
			render_pass.End();
			render_pass = nullptr;
		}

		void operator()(BindPipeline const& value) {
			auto const& pipeline = pipelines[value.pipeline].get();
			if (pipeline.compute) {
				compute_pipeline = std::get<wgpu::ComputePipeline>(pipeline.impl);
			}
			else {
				if (render_pass) {
					render_pass.SetPipeline(std::get<wgpu::RenderPipeline>(pipeline.impl));
				}
				else {
					// Deferred until BeginRendering opens the pass.
					pending_pipeline = std::get<wgpu::RenderPipeline>(pipeline.impl);
				}
			}
		}

		void operator()(BindResourceGroup const& value) {
			auto const& group = groups[value.group].get();
			if (render_pass) {
				render_pass.SetBindGroup(value.index, group.impl);
			}
			else if (compute_pass) {
				compute_pass.SetBindGroup(value.index, group.impl);
			}
			else {
				pending_bind_groups.push_back({ value.index, group.impl });
			}
		}

		void operator()(BindVertexBuffer const& value) {
			if (render_pass) {
				render_pass.SetVertexBuffer(
					value.slot,
					BufferAt(value.resource),
					value.offset,
					wgpu::kWholeSize
				);
			}
			else {
				pending_vertex_buffers.push_back({
					value.slot,
					BufferAt(value.resource),
					value.offset
				});
			}
		}

		void operator()(BindIndexBuffer const& value) {
			if (render_pass) {
				render_pass.SetIndexBuffer(
					BufferAt(value.resource),
					NativeIndexFormat(value.type),
					value.offset,
					wgpu::kWholeSize
				);
			}
			else {
				pending_index_buffer = IndexBufferPending{
					BufferAt(value.resource),
					NativeIndexFormat(value.type),
					value.offset
				};
			}
		}

		void operator()(Viewport const& value) {
			if (render_pass) {
				render_pass.SetViewport(value.x, value.y, value.width, value.height, value.minimum_depth, value.maximum_depth);
			}
			else {
				pending_viewport = ViewportPending{ value.x, value.y, value.width, value.height, value.minimum_depth, value.maximum_depth };
			}
		}

		void operator()(Scissor const& value) {
			if (render_pass) {
				render_pass.SetScissorRect(
					static_cast<std::uint32_t>(value.x),
					static_cast<std::uint32_t>(value.y),
					static_cast<std::uint32_t>(value.width),
					static_cast<std::uint32_t>(value.height)
				);
			}
			else {
				pending_scissor = ScissorPending{
					static_cast<std::uint32_t>(value.x),
					static_cast<std::uint32_t>(value.y),
					static_cast<std::uint32_t>(value.width),
					static_cast<std::uint32_t>(value.height)
				};
			}
		}

		void operator()(Draw const& value) {
			if (!render_pass) {
				throw std::logic_error("WebGPU draw requires an active render pass");
			}
			render_pass.Draw(value.vertex_count, value.instance_count, value.first_vertex, value.first_instance);
		}

		void operator()(DrawIndexed const& value) {
			if (!render_pass) {
				throw std::logic_error("WebGPU indexed draw requires an active render pass");
			}
			render_pass.DrawIndexed(
				value.index_count,
				value.instance_count,
				value.first_index,
				value.vertex_offset,
				value.first_instance
			);
		}

		void operator()(Dispatch const& value) {
			if (!compute_pass) {
				wgpu::ComputePassDescriptor descriptor;
				compute_pass = encoder.BeginComputePass(&descriptor);
				if (compute_pipeline) {
					compute_pass.SetPipeline(compute_pipeline);
				}
				for (auto const& group : pending_bind_groups) {
					compute_pass.SetBindGroup(group.index, group.group);
				}
				pending_bind_groups.clear();
			}
			else if (compute_pipeline) {
				compute_pass.SetPipeline(compute_pipeline);
			}
			compute_pass.DispatchWorkgroups(value.group_count_x, value.group_count_y, value.group_count_z);
		}

		void operator()(CopyBuffer const& value) {
			EndPass();
			encoder.CopyBufferToBuffer(
				BufferAt(value.source),
				value.source_offset,
				BufferAt(value.destination),
				value.destination_offset,
				value.size
			);
		}

		void operator()(CopyBufferToTexture const& value) {
			EndPass();
			wgpu::TexelCopyBufferInfo source;
			source.buffer = BufferAt(value.source);
			source.layout.offset = value.source_layout.offset;
			source.layout.bytesPerRow = value.source_layout.bytes_per_row;
			source.layout.rowsPerImage = value.source_layout.rows_per_image;
			wgpu::TexelCopyTextureInfo destination;
			destination.texture = TextureAt(value.destination);
			destination.mipLevel = value.destination_region.mip_level;
			destination.origin = { value.destination_region.offset_x, value.destination_region.offset_y, value.destination_region.offset_z };
			destination.aspect = wgpu::TextureAspect::All;
			wgpu::Extent3D extent{ value.destination_region.width, value.destination_region.height, value.destination_region.depth };
			encoder.CopyBufferToTexture(&source, &destination, &extent);
		}

		void operator()(CopyTextureToBuffer const& value) {
			EndPass();
			wgpu::TexelCopyTextureInfo source;
			source.texture = TextureAt(value.source);
			source.mipLevel = value.source_region.mip_level;
			source.origin = { value.source_region.offset_x, value.source_region.offset_y, value.source_region.offset_z };
			source.aspect = wgpu::TextureAspect::All;
			wgpu::TexelCopyBufferInfo destination;
			destination.buffer = BufferAt(value.destination);
			destination.layout.offset = value.destination_layout.offset;
			destination.layout.bytesPerRow = value.destination_layout.bytes_per_row;
			destination.layout.rowsPerImage = value.destination_layout.rows_per_image;
			wgpu::Extent3D extent{ value.source_region.width, value.source_region.height, value.source_region.depth };
			encoder.CopyTextureToBuffer(&source, &destination, &extent);
		}

		void operator()(CopyTexture const& value) {
			EndPass();
			wgpu::TexelCopyTextureInfo source;
			source.texture = TextureAt(value.source);
			source.mipLevel = value.source_region.mip_level;
			source.origin = { value.source_region.offset_x, value.source_region.offset_y, value.source_region.offset_z };
			source.aspect = wgpu::TextureAspect::All;
			wgpu::TexelCopyTextureInfo destination;
			destination.texture = TextureAt(value.destination);
			destination.mipLevel = value.destination_region.mip_level;
			destination.origin = { value.destination_region.offset_x, value.destination_region.offset_y, value.destination_region.offset_z };
			destination.aspect = wgpu::TextureAspect::All;
			wgpu::Extent3D extent{ value.source_region.width, value.source_region.height, value.source_region.depth };
			encoder.CopyTextureToTexture(&source, &destination, &extent);
		}

		void operator()(WriteBuffer const& value) {
			EndPass();
			auto const& buffer = BufferAt(value.resource);
			if (value.offset > buffer.GetSize() || value.data.size() > buffer.GetSize() - value.offset) {
				throw std::out_of_range("WebGPU write exceeds the buffer size");
			}
			std::atomic<bool> done = false;
			std::exception_ptr error;
			buffer.MapAsync(
				wgpu::MapMode::Write,
				value.offset,
				value.data.size(),
				wgpu::CallbackMode::AllowProcessEvents,
				[&](wgpu::MapAsyncStatus status, wgpu::StringView message) {
					if (status != wgpu::MapAsyncStatus::Success) {
						error = std::make_exception_ptr(std::runtime_error(std::format(
							"WebGPU buffer map failed with status {}: {}",
							static_cast<int>(status),
							std::string_view(message.data, message.length)
						)));
					}
					done.store(true, std::memory_order_release);
				}
			);
			while (!done.load(std::memory_order_acquire)) {
				instance.ProcessEvents();
			}
			if (error) {
				std::rethrow_exception(error);
			}
			void* pointer = buffer.GetMappedRange(value.offset, value.data.size());
			std::memcpy(pointer, value.data.data(), value.data.size());
			buffer.Unmap();
		}

		void operator()(Present const& value) {
			EndPass();
			if (presentation_cursor >= presentations.size()) {
				throw std::logic_error("WebGPU presentation work was not prepared");
			}
			auto const& work = presentations[presentation_cursor++];
			if (work.source != value.source) {
				throw std::logic_error("WebGPU presentation work does not match its command");
			}
			auto source_texture = TextureAt(value.source);
			wgpu::TexelCopyTextureInfo source;
			source.texture = source_texture;
			source.mipLevel = 0u;
			source.origin = {};
			source.aspect = wgpu::TextureAspect::All;
			wgpu::TexelCopyTextureInfo destination;
			destination.texture = work.current_texture;
			destination.mipLevel = 0u;
			destination.origin = {};
			destination.aspect = wgpu::TextureAspect::All;
			wgpu::Extent3D extent{ source_texture.GetWidth(), source_texture.GetHeight(), 1u };
			encoder.CopyTextureToTexture(&source, &destination, &extent);
		}
	};

	Backend::CompletionToken Backend::ExecuteCommands(
		SchedulerContext const& scheduler,
		execution::ExecutionPlan const& plan,
		std::span<PlatformHandle const> presentation_targets,
		std::span<std::reference_wrapper<Resource> const> resources,
		std::span<std::reference_wrapper<View> const> views,
		std::span<std::reference_wrapper<Sampler> const> samplers,
		std::span<std::reference_wrapper<Pipeline> const> pipelines,
		std::span<std::reference_wrapper<PipelineResourceGroup> const> resource_groups,
		execution::StopTokenView stop_token
	) {
		if (!scheduler.impl) {
			throw std::invalid_argument("WebGPU scheduler is not initialized");
		}
		// Phase 1a: validate all caller-owned data before observing stop.
		if (resources.size() != plan.bindings.resource_count ||
			views.size() != plan.bindings.view_count ||
			samplers.size() != plan.bindings.sampler_count ||
			pipelines.size() != plan.bindings.pipeline_count ||
			resource_groups.size() != plan.bindings.resource_group_count) {
			throw std::invalid_argument("WebGPU execution binding count mismatch");
		}
		std::vector<PlatformHandle> active_presentation_targets;
		for (std::size_t index = 0u; index < plan.batches.size(); ++index) {
			auto const& batch = plan.batches[index];
			if (batch.id != index) {
				throw std::invalid_argument("WebGPU execution batch IDs must match storage indices");
			}
			for (auto dependency : batch.dependencies) {
				if (dependency >= index) {
					throw std::invalid_argument("WebGPU execution batches are not topologically ordered");
				}
			}
			for (auto const& node : batch.nodes) {
				for (auto const& command : node.commands) {
					auto present = std::get_if<Present>(&command);
					if (!present) {
						continue;
					}
					if (present->target >= presentation_targets.size()) {
						throw std::invalid_argument("WebGPU presentation binding is invalid");
					}
					auto target = presentation_targets[present->target];
					if (std::ranges::find(active_presentation_targets, target) !=
						active_presentation_targets.end()) {
						throw std::invalid_argument(
							"WebGPU execution cannot present one target more than once"
						);
					}
					active_presentation_targets.emplace_back(target);
				}
			}
		}

		auto token_state = std::unique_ptr<CompletionToken::Implementation, CompletionToken::Deleter>(
			new CompletionToken::Implementation()
		);
		token_state->instance = scheduler.impl->instance;
		auto* token_pointer = token_state.get();
		if (stop_token.stop_requested()) {
			token_state->stopped.store(true, std::memory_order_release);
			token_state->complete.store(true, std::memory_order_release);
			return CompletionToken(std::move(token_state));
		}

		try {
			// Phase 1: acquire every presentation surface's current texture. This is
			// a blocking CPU call; doing it up front keeps failures deterministic.
			std::vector<PresentationWork> presentations;
			for (auto const& batch : plan.batches) {
				for (auto const& node : batch.nodes) {
					for (auto const& command : node.commands) {
						auto present = std::get_if<Present>(&command);
						if (!present) {
							continue;
						}
						auto const& source = std::get<wgpu::Texture>(resources[present->source].get());
						Backend::SchedulerContext::Implementation::SurfaceState* surface_state = nullptr;
						{
							std::lock_guard lock(scheduler.impl->surface_mutex);
							for (auto& candidate : scheduler.impl->surfaces) {
								if (candidate.handle == presentation_targets[present->target]) {
									surface_state = &candidate;
									break;
								}
							}
							if (!surface_state) {
								scheduler.impl->surfaces.push_back({});
								surface_state = &scheduler.impl->surfaces.back();
								surface_state->handle = presentation_targets[present->target];
								surface_state->surface = CreateSurfaceFor(
									scheduler.impl->instance,
									surface_state->handle
								);
							}
							auto width = source.GetWidth();
							auto height = source.GetHeight();
							auto format = source.GetFormat();
							if (surface_state->width != width ||
								surface_state->height != height ||
								surface_state->format != format) {
								wgpu::SurfaceConfiguration config;
								config.device = scheduler.impl->device;
								config.format = format;
								config.width = width;
								config.height = height;
								config.usage = wgpu::TextureUsage::CopyDst;
								config.presentMode = wgpu::PresentMode::Fifo;
								surface_state->surface.Configure(&config);
								surface_state->width = width;
								surface_state->height = height;
								surface_state->format = format;
							}
						}
						wgpu::SurfaceTexture current;
						surface_state->surface.GetCurrentTexture(&current);
						if (!current.texture) {
							throw std::runtime_error("WebGPU surface current texture is invalid");
						}
						presentations.emplace_back(
							PresentationWork{ present->source, surface_state->surface, current.texture }
						);
					}
				}
			}

			// Encode one command buffer per batch; WebGPU executes the submission in order.
			std::vector<wgpu::CommandBuffer> command_buffers;
			command_buffers.reserve(plan.batches.size());
			for (auto const& batch : plan.batches) {
				auto encoder = scheduler.impl->device.CreateCommandEncoder();
				Replayer replayer{
					resources,
					views,
					pipelines,
					resource_groups,
					presentations,
					scheduler.impl->instance,
					0u,
					encoder,
					{},
					{},
					{}
				};
				for (auto const& node : batch.nodes) {
					for (auto const& command : node.commands) {
						std::visit(replayer, command);
					}
				}
				replayer.EndPass();
				command_buffers.emplace_back(encoder.Finish());
			}

			auto queue = scheduler.impl->device.GetQueue();
			queue.Submit(command_buffers.size(), command_buffers.data());
			queue.OnSubmittedWorkDone(
				wgpu::CallbackMode::AllowProcessEvents,
				[token_pointer](wgpu::QueueWorkDoneStatus status, wgpu::StringView message) {
					if (status != wgpu::QueueWorkDoneStatus::Success) {
						token_pointer->error = std::make_exception_ptr(
							std::runtime_error(std::format(
								"WebGPU queue work failed with status {}: {}",
								static_cast<int>(status),
								std::string_view(message.data, message.length)
							))
						);
					}
					token_pointer->complete.store(true, std::memory_order_release);
				}
			);
			for (auto const& work : presentations) {
				auto status = work.surface.Present();
				if (!status && !token_state->error) {
					token_state->error = std::make_exception_ptr(
						std::runtime_error("WebGPU surface present failed")
					);
				}
			}
		}
		catch (...) {
			token_state->error = std::current_exception();
			token_state->complete.store(true, std::memory_order_release);
		}
		return CompletionToken(std::move(token_state));
	}

	bool Backend::CompletionToken::Poll() noexcept {
		if (!impl) {
			return true;
		}
		// Drive the OnSubmittedWorkDone callback synchronously: WebGPU needs no
		// scheduler thread, so the callback fires here on the polling thread.
		impl->instance.ProcessEvents();
		return impl->complete.load(std::memory_order_acquire);
	}

	std::exception_ptr Backend::CompletionToken::Error() const noexcept {
		return impl ? impl->error : nullptr;
	}

	bool Backend::CompletionToken::IsStopped() const noexcept {
		return impl && impl->stopped.load(std::memory_order_acquire);
	}

} // namespace fyuu_rhi::webgpu
