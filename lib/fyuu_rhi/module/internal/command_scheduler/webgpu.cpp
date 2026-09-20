module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <algorithm>
#include <functional>
#include <iterator>

#include <type_traits>

#include <string>

#include <limits>

#include <cstdint>

#include <chrono>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <optional>
#include <variant>

#include <ranges>
#include <span>

#include <format>

#include <stop_token>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <wayland-client-core.h>
#include <wayland-util.h>
#elif defined(__ANDROID__)
#include <android/native_window.h>
#endif // defined(_WIN32)
#include <dawn/webgpu_cpp.h>

module fyuu_rhi:webgpu_command_scheduler;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :command_scheduler_dispatch;
import :command_scheduler_factory;
import :completion_token_factory;
import :execution;
import :logical_device_dispatch;
import :pipeline;
import :pipeline_factory;
import :pipeline_resource_group_factory;
import :resource_factory;
import :sampler_factory;
import :view_factory;
import :webgpu_data;
import :webgpu_utility;

extern "C" void ParallelFor(
    std::size_t first,
    std::size_t last,
    void* function,
    void (*invoke)(void*, std::size_t)
);

namespace {

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::execution;

	template <class Function>
	void ParallelFor(std::size_t first, std::size_t last, Function&& function) {
		::ParallelFor(
		    first,
		    last,
		    std::addressof(function),
		    [](void* erased_function, std::size_t index) {
			    (*static_cast<std::remove_reference_t<Function>*>(erased_function))(index);
		    }
		);
	}

	wgpu::LoadOp NativeLoadOp(LoadOperation operation) noexcept {
		switch (operation) {
			case LoadOperation::Load:
				return wgpu::LoadOp::Load;
			case LoadOperation::Clear:
				return wgpu::LoadOp::Clear;
			case LoadOperation::Discard:
				return wgpu::LoadOp::Clear;
			default:
				return wgpu::LoadOp::Load;
		}
	}

	wgpu::StoreOp NativeStoreOp(StoreOperation operation) noexcept {
		switch (operation) {
			case StoreOperation::Store:
				return wgpu::StoreOp::Store;
			case StoreOperation::Discard:
				return wgpu::StoreOp::Discard;
			default:
				return wgpu::StoreOp::Store;
		}
	}

	wgpu::IndexFormat NativeIndexFormat(IndexType type) noexcept {
		if (type == IndexType::Uint16) {
			return wgpu::IndexFormat::Uint16;
		}
		return wgpu::IndexFormat::Uint32;
	}

	wgpu::Surface CreateSurface(wgpu::Instance const& instance, PlatformHandle const& handle) {
#if defined(_WIN32)
		wgpu::SurfaceSourceWindowsHWND source{};
		source.hinstance = GetModuleHandle(nullptr);
		source.hwnd = handle;
		wgpu::SurfaceDescriptor descriptor{};
		descriptor.nextInChain = &source;
		return instance.CreateSurface(&descriptor);
#elif defined(__ANDROID__)
		wgpu::SurfaceDescriptorFromAndroidNativeWindow source{};
		source.window = handle;
		wgpu::SurfaceDescriptor descriptor{};
		descriptor.nextInChain = &source;
		return instance.CreateSurface(&descriptor);
#elif defined(__linux__)
		return std::visit(
		    [&instance](auto const& native) {
			    using Native = std::remove_cvref_t<decltype(native)>;
			    if constexpr (std::same_as<Native, X11PlatformHandle>) {
				    wgpu::SurfaceDescriptorFromXlibWindow source{};
				    source.display = native.display;
				    source.window = native.window;
				    wgpu::SurfaceDescriptor descriptor{};
				    descriptor.nextInChain = &source;
				    return instance.CreateSurface(&descriptor);
			    } else {
				    wgpu::SurfaceDescriptorFromWaylandSurface source{};
				    source.display = native.display;
				    source.surface = native.surface;
				    wgpu::SurfaceDescriptor descriptor{};
				    descriptor.nextInChain = &source;
				    return instance.CreateSurface(&descriptor);
			    }
		    },
		    handle
		);
#endif // defined(_WIN32)
	}

} // namespace

namespace fyuu_rhi::webgpu {

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
		std::span<std::reference_wrapper<webgpu::Resource const> const> resources;
		/// The flags of those resources, by the same index. Dawn only tells an attachment's
		/// format through the texture its view was made from, which its C++ view type does not
		/// expose, so the depth format is read from the RHI's own description instead.
		std::span<ResourceFlags const> resource_flags;
		std::span<std::reference_wrapper<webgpu::View const> const> views;
		std::span<std::reference_wrapper<webgpu::Pipeline const> const> pipelines;
		std::span<std::reference_wrapper<webgpu::PipelineResourceGroup const> const> groups;
		std::vector<PresentationWork> const& presentations;
		std::size_t presentation_cursor = 0u;

		wgpu::Instance instance;
		wgpu::CommandEncoder encoder;
		wgpu::RenderPassEncoder render_pass;
		wgpu::ComputePassEncoder compute_pass;
		wgpu::ComputePipeline compute_pipeline;
		webgpu::Pipeline const* pipeline = nullptr;

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
		struct ConstantState {
			webgpu::Pipeline::ConstantRange const* range;
			std::vector<std::byte> data;
		};

		wgpu::RenderPipeline pending_pipeline;
		std::vector<VertexBufferPending> pending_vertex_buffers;
		std::optional<IndexBufferPending> pending_index_buffer;
		std::optional<ViewportPending> pending_viewport;
		std::optional<ScissorPending> pending_scissor;
		std::vector<BindGroupPending> pending_bind_groups;
		std::vector<SetPipelineConstants const*> pending_constants;
		std::vector<ConstantState> constants;
		std::vector<wgpu::Buffer> constant_buffers;
		std::vector<wgpu::BindGroup> constant_groups;
		std::vector<wgpu::BindGroup> dynamic_groups;

		wgpu::BindGroup BindGroup(
		    webgpu::PipelineResourceGroup const& group,
		    std::span<std::size_t const> dynamic_offsets
		) {
			if (dynamic_offsets.empty()) {
				return group.impl;
			}
			if (dynamic_offsets.size() != group.dynamic_buffers.size()) {
				throw std::invalid_argument(
				    "WebGPU dynamic-offset count does not match the resource group"
				);
			}
			auto entries = group.entries;
			std::ranges::for_each(
			    std::views::iota(std::size_t{0u}, group.dynamic_buffers.size()),
			    [&](std::size_t index) {
				    auto const& binding = group.dynamic_buffers[index];
				    auto offset = dynamic_offsets[index];
				    if (offset > binding.capacity - binding.base_offset ||
					    binding.size > binding.capacity - binding.base_offset - offset) {
					    throw std::out_of_range(
					        "WebGPU dynamic buffer offset exceeds the bound buffer"
					    );
				    }
				    entries[binding.entry].offset = binding.base_offset + offset;
				    entries[binding.entry].size = binding.size;
			    }
			);
			wgpu::BindGroupDescriptor descriptor{
			    .layout = group.layout,
			    .entryCount = entries.size(),
			    .entries = entries.data()
			};
			auto result = group.device.CreateBindGroup(&descriptor);
			dynamic_groups.emplace_back(result);
			return result;
		}

		webgpu::Pipeline::ConstantRange const& ConstantRange(
		    SetPipelineConstants const& value
		) const {
			if (!pipeline) {
				throw std::logic_error("WebGPU pipeline constants require a bound pipeline");
			}
			// Resolved against the pipeline bound earlier in this command list, not the one
			// bound at draw time: SetPipelineConstants must follow its BindPipeline.
			auto range =
			    std::ranges::find_if(pipeline->constant_ranges, [&value](auto const& candidate) {
				    return candidate.abi_slot == value.slot && candidate.abi_space == value.space;
			    });
			if (range == pipeline->constant_ranges.end()) {
				throw std::invalid_argument(
				    "WebGPU pipeline has no matching pipeline-constant range"
				);
			}
			if (value.offset > range->size || value.data.size() > range->size - value.offset) {
				throw std::out_of_range(
				    "WebGPU pipeline-constant write exceeds its reflected range"
				);
			}
			return *range;
		}

		void SetConstants(SetPipelineConstants const& value) {
			auto const& range = ConstantRange(value);
			if (!pipeline->native_immediates) {
				auto state = std::ranges::find_if(constants, [&range](auto const& candidate) {
					return candidate.range == &range;
				});
				if (state == constants.end()) {
					state = constants.emplace(
					    constants.end(),
					    ConstantState{&range, std::vector<std::byte>(range.size)}
					);
				}
				std::ranges::copy(value.data, state->data.begin() + value.offset);
				// Pass-encoder state can only be set on an open pass, so a write recorded
				// before BeginRendering is replayed by FlushRenderState once the pass exists.
				if (!render_pass && !compute_pass) {
					pending_constants.emplace_back(&value);
					return;
				}

				std::vector<wgpu::BindGroupEntry> entries;
				entries.reserve(pipeline->constant_ranges.size());
				std::ranges::for_each(pipeline->constant_ranges, [&](auto const& constant_range) {
					auto source = std::ranges::find_if(constants, [&](auto const& candidate) {
						return candidate.range == &constant_range;
					});
					if (source == constants.end()) {
						source = constants.emplace(
						    constants.end(),
						    ConstantState{
						        &constant_range,
						        std::vector<std::byte>(constant_range.size)
						    }
						);
					}
					wgpu::BufferDescriptor descriptor{
					    .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
					    .size = constant_range.size
					};
					auto buffer = pipeline->device.CreateBuffer(&descriptor);
					pipeline->device.GetQueue().WriteBuffer(
					    buffer,
					    0u,
					    source->data.data(),
					    source->data.size()
					);
					entries.emplace_back(
					    wgpu::BindGroupEntry{
					        .binding = constant_range.binding,
					        .buffer = buffer,
					        .offset = 0u,
					        .size = constant_range.size
					    }
					);
					constant_buffers.emplace_back(std::move(buffer));
				});
				wgpu::BindGroupDescriptor descriptor{
				    .layout = pipeline->bind_group_layouts[pipeline->constant_group],
				    .entryCount = entries.size(),
				    .entries = entries.data()
				};
				auto group = pipeline->device.CreateBindGroup(&descriptor);
				if (render_pass) {
					render_pass.SetBindGroup(pipeline->constant_group, group);
				} else if (compute_pass) {
					compute_pass.SetBindGroup(pipeline->constant_group, group);
				}
				constant_groups.emplace_back(std::move(group));
				return;
			}
			if (render_pass) {
				render_pass.SetImmediates(
				    range.offset + value.offset,
				    value.data.data(),
				    value.data.size()
				);
				return;
			}
			if (compute_pass) {
				compute_pass.SetImmediates(
				    range.offset + value.offset,
				    value.data.data(),
				    value.data.size()
				);
				return;
			}
			pending_constants.emplace_back(&value);
		}

		void FlushRenderState() {
			if (pending_pipeline) {
				render_pass.SetPipeline(pending_pipeline);
				pending_pipeline = nullptr;
			}
			for (auto const& vertex : pending_vertex_buffers) {
				render_pass.SetVertexBuffer(
				    vertex.slot,
				    vertex.buffer,
				    vertex.offset,
				    wgpu::kWholeSize
				);
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
			std::ranges::for_each(pending_constants, [this](auto value) {
				SetConstants(*value);
			});
			pending_constants.clear();
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
			auto const* view = std::get_if<wgpu::TextureView>(&views[index].get().impl);
			if (!view) {
				throw std::invalid_argument("WebGPU command requires a texture view");
			}
			return *view;
		}

		wgpu::Texture TextureAt(std::size_t index) const {
			auto const* texture = std::get_if<wgpu::Texture>(&resources[index].get().impl);
			if (!texture) {
				throw std::invalid_argument("WebGPU command requires a texture resource");
			}
			return *texture;
		}

		wgpu::Buffer BufferAt(std::size_t index) const {
			auto const* buffer = std::get_if<wgpu::Buffer>(&resources[index].get().impl);
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
					attachment.clearValue = wgpu::Color{
					    color.clear.red,
					    color.clear.green,
					    color.clear.blue,
					    color.clear.alpha
					};
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
				// A depth-only attachment must leave the stencil operations at their undefined
				// default. Dawn rejects a stencil load/store pair on a texture with no stencil
				// aspect ("Both stencilLoadOp ... and stencilStoreOp ... must not be set if the
				// attachment ... has no stencil aspect"), and the RHI's Discard defaults map to
				// Clear/Discard, which is exactly the rejected combination.
				if (webgpu::HasStencilAspect(resource_flags[attachment.resource])) {
					ds.stencilLoadOp = NativeLoadOp(attachment.stencil_load);
					ds.stencilStoreOp = NativeStoreOp(attachment.stencil_store);
				}
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
			pipeline = &pipelines[value.pipeline].get();
			if (auto compute = std::get_if<wgpu::ComputePipeline>(&pipeline->impl)) {
				compute_pipeline = *compute;
				return;
			}
			auto render = std::get_if<wgpu::RenderPipeline>(&pipeline->impl);
			if (!render) {
				throw std::invalid_argument("WebGPU command uses an empty pipeline");
			}
			if (render_pass) {
				render_pass.SetPipeline(*render);
			} else {
				pending_pipeline = *render;
			}
		}

		void operator()(BindResourceGroup const& value) {
			auto const& group = groups[value.group].get();
			if (group.space != value.space) {
				throw std::invalid_argument("WebGPU resource group space mismatch");
			}
			auto impl = BindGroup(group, value.additional_buffer_offsets);
			if (render_pass) {
				render_pass.SetBindGroup(value.space, impl);
			} else if (compute_pass) {
				compute_pass.SetBindGroup(value.space, impl);
			} else {
				pending_bind_groups.push_back({value.space, impl});
			}
		}

		void operator()(SetPipelineConstants const& value) {
			SetConstants(value);
		}

		void operator()(BindVertexBuffer const& value) {
			if (render_pass) {
				render_pass.SetVertexBuffer(
				    value.slot,
				    BufferAt(value.resource),
				    value.offset,
				    wgpu::kWholeSize
				);
			} else {
				pending_vertex_buffers.push_back(
				    {value.slot, BufferAt(value.resource), value.offset}
				);
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
			} else {
				pending_index_buffer = IndexBufferPending{
				    BufferAt(value.resource),
				    NativeIndexFormat(value.type),
				    value.offset
				};
			}
		}

		void operator()(Viewport const& value) {
			if (render_pass) {
				render_pass.SetViewport(
				    value.x,
				    value.y,
				    value.width,
				    value.height,
				    value.minimum_depth,
				    value.maximum_depth
				);
			} else {
				pending_viewport = ViewportPending{
				    value.x,
				    value.y,
				    value.width,
				    value.height,
				    value.minimum_depth,
				    value.maximum_depth
				};
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
			} else {
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
			render_pass.Draw(
			    value.vertex_count,
			    value.instance_count,
			    value.first_vertex,
			    value.first_instance
			);
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
				std::ranges::for_each(pending_constants, [this](auto constant) {
					SetConstants(*constant);
				});
				pending_constants.clear();
			} else if (compute_pipeline) {
				compute_pass.SetPipeline(compute_pipeline);
			}
			compute_pass.DispatchWorkgroups(
			    value.group_count_x,
			    value.group_count_y,
			    value.group_count_z
			);
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
			destination.origin = {
			    value.destination_region.offset_x,
			    value.destination_region.offset_y,
			    value.destination_region.offset_z
			};
			destination.aspect = wgpu::TextureAspect::All;
			wgpu::Extent3D extent{
			    value.destination_region.width,
			    value.destination_region.height,
			    value.destination_region.depth
			};
			encoder.CopyBufferToTexture(&source, &destination, &extent);
		}

		void operator()(CopyTextureToBuffer const& value) {
			EndPass();
			wgpu::TexelCopyTextureInfo source;
			source.texture = TextureAt(value.source);
			source.mipLevel = value.source_region.mip_level;
			source.origin = {
			    value.source_region.offset_x,
			    value.source_region.offset_y,
			    value.source_region.offset_z
			};
			source.aspect = wgpu::TextureAspect::All;
			wgpu::TexelCopyBufferInfo destination;
			destination.buffer = BufferAt(value.destination);
			destination.layout.offset = value.destination_layout.offset;
			destination.layout.bytesPerRow = value.destination_layout.bytes_per_row;
			destination.layout.rowsPerImage = value.destination_layout.rows_per_image;
			wgpu::Extent3D extent{
			    value.source_region.width,
			    value.source_region.height,
			    value.source_region.depth
			};
			encoder.CopyTextureToBuffer(&source, &destination, &extent);
		}

		void operator()(CopyTexture const& value) {
			EndPass();
			wgpu::TexelCopyTextureInfo source;
			source.texture = TextureAt(value.source);
			source.mipLevel = value.source_region.mip_level;
			source.origin = {
			    value.source_region.offset_x,
			    value.source_region.offset_y,
			    value.source_region.offset_z
			};
			source.aspect = wgpu::TextureAspect::All;
			wgpu::TexelCopyTextureInfo destination;
			destination.texture = TextureAt(value.destination);
			destination.mipLevel = value.destination_region.mip_level;
			destination.origin = {
			    value.destination_region.offset_x,
			    value.destination_region.offset_y,
			    value.destination_region.offset_z
			};
			destination.aspect = wgpu::TextureAspect::All;
			wgpu::Extent3D extent{
			    value.source_region.width,
			    value.source_region.height,
			    value.source_region.depth
			};
			encoder.CopyTextureToTexture(&source, &destination, &extent);
		}

		void operator()(WriteBuffer const& value) {
			EndPass();
			auto const& buffer = BufferAt(value.resource);
			auto buffer_size = buffer.GetSize();
			if (value.offset > buffer_size || value.data.size() > buffer_size - value.offset) {
				throw std::out_of_range("WebGPU write exceeds the buffer size");
			}
			if (buffer.GetUsage() & wgpu::BufferUsage::MapWrite) {
				// This operator runs inside the parallel recording pass, so it must not
				// block: the serial pre-map pass has already mapped every buffer a
				// WriteBuffer targets and the unmapping pass releases them once every
				// batch has finished recording.
				if (buffer.GetMapState() != wgpu::BufferMapState::Mapped) {
					throw std::logic_error(
					    "WebGPU upload buffer was not mapped before recording"
					);
				}
				auto destination = static_cast<std::byte*>(buffer.GetMappedRange());
				if (!destination) {
					throw std::runtime_error("WebGPU returned an empty mapped upload range");
				}
				std::ranges::copy(value.data, destination + value.offset);
				return;
			}
			encoder.WriteBuffer(
			    buffer,
			    value.offset,
			    reinterpret_cast<std::uint8_t const*>(value.data.data()),
			    value.data.size()
			);
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
			wgpu::Extent3D extent{source_texture.GetWidth(), source_texture.GetHeight(), 1u};
			encoder.CopyTextureToTexture(&source, &destination, &extent);
		}
	};

} // namespace fyuu_rhi::webgpu

namespace fyuu_rhi::webgpu {

	namespace {

		/// Publishes one completion under the state's own mutex and then notifies.
		///
		/// A waiting thread re-tests the flag while holding that same mutex, so storing it
		/// before the notify cannot be missed between that test and the wait. An already
		/// recorded error is kept, so a failure Dawn reported for the token is not replaced
		/// by the device-loss error published for the same token.
		void PublishCompletion(
			std::shared_ptr<CompletionState> const& state,
			std::exception_ptr error = {}
		) {
			{
				std::unique_lock lock(state->mutex);
				if (error && !state->error) {
					state->error = std::move(error);
				}
				state->complete.store(true, std::memory_order_release);
			}
			state->condition.notify_all();
		}

	} // namespace

	void CommandSchedulerContext::Watch(
		wgpu::Future future,
		std::shared_ptr<CompletionState> const& state
	) {
		if (!state || future.id == 0u) {
			return;
		}
		std::unique_lock lock(pump_mutex);
		if (pump_device_lost) {
			auto error = pump_device_lost_error;
			lock.unlock();
			PublishCompletion(state, error);
			return;
		}
		pump_futures.push_back(WatchedFuture{future, state});
		lock.unlock();
		pump_condition.notify_all();
	}

	void CommandSchedulerContext::Run(std::stop_token stop) {
		// Device loss is only observable through this future: it is a plain wait-list
		// event, so it can be waited on together with queue futures at a zero timeout, and
		// Dawn untracks it once it fires, from which point on every wait reports it as
		// already complete.
		auto lost_future = device.GetLostFuture();

		context_ready.store(true, std::memory_order_release);
		context_ready.notify_all();

		bool device_lost = false;
		std::exception_ptr device_lost_error;

		while (!stop.stop_requested()) {
			std::vector<WatchedFuture> pending;
			std::vector<wgpu::FutureWaitInfo> waits;
			try {
				{
					std::unique_lock lock(pump_mutex);
					// Bounded sleep between rounds, not a wait for completion. The wait
					// below is a zero-timeout poll because that is the only timeout that
					// does not hold the device-wide lock for its whole duration on the
					// D3D12 backend, and zero is legal across queues and event kinds. The
					// destructor notifies this condition after requesting the stop, so the
					// join does not have to wait the interval out.
					(void)pump_condition.wait_for(lock, std::chrono::milliseconds(1));
					if (stop.stop_requested()) {
						break;
					}
					pending = pump_futures;
				}

				waits.reserve(pending.size() + 1u);
				std::ranges::for_each(pending, [&waits](WatchedFuture const& watched) {
					waits.emplace_back(wgpu::FutureWaitInfo{watched.future});
				});
				waits.emplace_back(wgpu::FutureWaitInfo{lost_future});
				(void)instance.WaitAny(waits.size(), waits.data(), 0u);

				if (waits.back().completed && !device_lost) {
					device_lost = true;
					device_lost_error = std::make_exception_ptr(
						std::runtime_error("WebGPU device was lost with work outstanding")
					);
					std::unique_lock lock(pump_mutex);
					pump_device_lost = true;
					pump_device_lost_error = device_lost_error;
				}
				for (std::size_t index = 0u; index < pending.size(); ++index) {
					// Once the device is gone, every outstanding token is reported as
					// failed: its work can never finish.
					if (device_lost || waits[index].completed) {
						PublishCompletion(pending[index].state, device_lost_error);
					}
				}
				// Drop everything terminal. The pump is the only publisher of completion
				// for a published future, so anything still incomplete stays watched.
				std::unique_lock lock(pump_mutex);
				std::erase_if(pump_futures, [](WatchedFuture const& watched) {
					return watched.state->complete.load(std::memory_order_acquire);
				});
			}
			catch (...) {
				// The pump is the only publisher of completion, so a failure here has to be
				// reported to everything outstanding instead of unwinding out of the
				// thread, which would terminate the process.
				auto failure = std::current_exception();
				std::unique_lock lock(pump_mutex);
				std::ranges::for_each(pump_futures, [failure](WatchedFuture const& watched) {
					PublishCompletion(watched.state, failure);
				});
				pump_futures.clear();
				break;
			}
		}

		// The pump has stopped, so nothing can complete a future any more. Report what is
		// still outstanding as cancelled rather than leaving a waiter blocked forever.
		std::unique_lock lock(pump_mutex);
		std::ranges::for_each(pump_futures, [](WatchedFuture& watched) {
			{
				std::unique_lock state_lock(watched.state->mutex);
				watched.state->stopped.store(true, std::memory_order_release);
			}
			PublishCompletion(watched.state, {});
		});
		pump_futures.clear();
	}

	CommandSchedulerContext::CommandSchedulerContext(CommandSchedulerContext&& other) noexcept
		: instance(std::move(other.instance)),
		device(std::move(other.device)),
		errors(std::move(other.errors)),
		surfaces(std::move(other.surfaces)),
		pump_thread(
			[this](std::stop_token stop_token) {
				Run(stop_token);
			}
		) {
		context_ready.wait(false, std::memory_order_acquire);
	}

	CommandSchedulerContext::~CommandSchedulerContext() noexcept {
		pump_thread.request_stop();
		// jthread::request_stop does not wake a condition variable, so without this the
		// join would block until the pump's own bounded wait elapsed.
		pump_condition.notify_all();
	}

} // namespace fyuu_rhi::webgpu

namespace fyuu_rhi {

	template <> struct CreateScheduler<webgpu::LogicalDevice> {
		webgpu::LogicalDevice* logical_device;

		execution::CommandScheduler operator()() const {
			return execution::MakeCommandScheduler(
			    webgpu::CommandSchedulerContext{
			        logical_device->instance,
			        logical_device->impl,
			        logical_device->errors
			    }
			);
		}
	};

} // namespace fyuu_rhi

namespace fyuu_rhi::execution {

	template <> struct ExecuteCommands<webgpu::CommandSchedulerContext> {
		webgpu::CommandSchedulerContext* context;

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
			std::vector<std::reference_wrapper<webgpu::Resource const>> resources;
			std::vector<ResourceFlags> resource_flags;
			std::vector<std::reference_wrapper<webgpu::View const>> views;
			std::vector<std::reference_wrapper<webgpu::Sampler const>> samplers;
			std::vector<std::reference_wrapper<webgpu::Pipeline const>> pipelines;
			std::vector<std::reference_wrapper<webgpu::PipelineResourceGroup const>>
			    resource_groups;
			resources.reserve(bound_resources.size());
			resource_flags.reserve(bound_resources.size());
			views.reserve(bound_views.size());
			samplers.reserve(bound_samplers.size());
			pipelines.reserve(bound_pipelines.size());
			resource_groups.reserve(bound_resource_groups.size());

			std::ranges::transform(
			    bound_resources,
			    std::back_inserter(resources),
			    [](Resource const& resource) -> webgpu::Resource const& {
				    if (!resource.m_impl) {
					    throw std::invalid_argument("A WebGPU execution resource is empty");
				    }
				    auto native = std::get_if<webgpu::Resource>(&resource.m_impl->native);
				    if (!native) {
					    throw std::invalid_argument(
					        "A WebGPU execution resource uses another backend"
					    );
				    }
				    return *native;
			    }
			);
			std::ranges::transform(
			    bound_resources,
			    std::back_inserter(resource_flags),
			    [](Resource const& resource) {
				    return resource.GetFlags();
			    }
			);
			std::ranges::transform(
			    bound_views,
			    std::back_inserter(views),
			    [](View const& view) -> webgpu::View const& {
				    if (!view.m_impl) {
					    throw std::invalid_argument("A WebGPU execution view is empty");
				    }
				    auto native = std::get_if<webgpu::View>(&view.m_impl->native);
				    if (!native) {
					    throw std::invalid_argument("A WebGPU execution view uses another backend");
				    }
				    return *native;
			    }
			);
			std::ranges::transform(
			    bound_samplers,
			    std::back_inserter(samplers),
			    [](Sampler const& sampler) -> webgpu::Sampler const& {
				    if (!sampler.m_impl) {
					    throw std::invalid_argument("A WebGPU execution sampler is empty");
				    }
				    auto native = std::get_if<webgpu::Sampler>(&sampler.m_impl->native);
				    if (!native) {
					    throw std::invalid_argument(
					        "A WebGPU execution sampler uses another backend"
					    );
				    }
				    return *native;
			    }
			);
			std::ranges::transform(
			    bound_pipelines,
			    std::back_inserter(pipelines),
			    [](Pipeline const& pipeline) -> webgpu::Pipeline const& {
				    if (!pipeline.m_impl) {
					    throw std::invalid_argument("A WebGPU execution pipeline is empty");
				    }
				    auto native = std::get_if<webgpu::Pipeline>(&pipeline.m_impl->native);
				    if (!native) {
					    throw std::invalid_argument(
					        "A WebGPU execution pipeline uses another backend"
					    );
				    }
				    return *native;
			    }
			);
			std::ranges::transform(
			    bound_resource_groups,
			    std::back_inserter(resource_groups),
			    [](PipelineResourceGroup const& group) -> webgpu::PipelineResourceGroup const& {
				    if (!group.m_impl) {
					    throw std::invalid_argument("A WebGPU execution resource group is empty");
				    }
				    auto native = std::get_if<webgpu::PipelineResourceGroup>(&group.m_impl->native);
				    if (!native) {
					    throw std::invalid_argument(
					        "A WebGPU execution resource group uses another backend"
					    );
				    }
				    return *native;
			    }
			);
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
					throw std::invalid_argument(
					    "WebGPU execution batch IDs must match storage indices"
					);
				}
				for (auto dependency : batch.dependencies) {
					if (dependency >= index) {
						throw std::invalid_argument(
						    "WebGPU execution batches are not topologically ordered"
						);
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

			auto token_state = std::make_shared<webgpu::CompletionState>();
			if (stop_token.stop_requested()) {
				token_state->stopped.store(true, std::memory_order_release);
				token_state->complete.store(true, std::memory_order_release);
				return MakeCompletionToken(
				    webgpu::CompletionToken{context->instance, std::move(token_state), {}}
				);
			}

			wgpu::Future completion_future{};
			try {
				// Phase 1: acquire every presentation surface's current texture. This is
				// a blocking CPU call; doing it up front keeps failures deterministic.
				std::vector<webgpu::PresentationWork> presentations;
				std::vector<std::size_t> presentation_offsets(plan.batches.size() + 1u);
				for (
				    std::size_t batch_index = 0u; batch_index < plan.batches.size(); ++batch_index
				) {
					presentation_offsets[batch_index] = presentations.size();
					auto const& batch = plan.batches[batch_index];
					for (auto const& node : batch.nodes) {
						for (auto const& command : node.commands) {
							auto present = std::get_if<Present>(&command);
							if (!present) {
								continue;
							}
							auto const& source =
							    std::get<wgpu::Texture>(resources[present->source].get().impl);
							wgpu::SurfaceTexture current;
							wgpu::Surface surface;
							{
								std::lock_guard lock(context->surfaces_mutex);
								webgpu::CommandSchedulerContext::SurfaceState* surface_state =
								    nullptr;
								for (auto& candidate : context->surfaces) {
									if (candidate.handle == presentation_targets[present->target]) {
										surface_state = &candidate;
										break;
									}
								}
								if (!surface_state) {
									context->surfaces.push_back({});
									surface_state = &context->surfaces.back();
									surface_state->handle = presentation_targets[present->target];
									surface_state->surface =
									    CreateSurface(context->instance, surface_state->handle);
								}
								auto width = source.GetWidth();
								auto height = source.GetHeight();
								auto format = source.GetFormat();
								if (surface_state->width != width ||
								    surface_state->height != height ||
								    surface_state->format != format) {
									wgpu::SurfaceConfiguration config;
									config.device = context->device;
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
								surface = surface_state->surface;
								// Blocking here is inherent to acquiring a surface image:
								// the texture is only handed out once the presentation
								// engine frees one, and there is no asynchronous form of
								// this call.
								surface.GetCurrentTexture(&current);
							}
							if (!current.texture) {
								throw std::runtime_error(
								    "WebGPU surface current texture is invalid"
								);
							}
							presentations.emplace_back(
							    webgpu::PresentationWork{present->source, surface, current.texture}
							);
						}
					}
				}
				presentation_offsets.back() = presentations.size();

				// Phase 1b: map every upload buffer the plan writes through a host
				// mapping. Mapping waits for the buffer's earlier GPU use, so issuing it
				// from inside a batch would both block the parallel recording pass and let
				// two batches sharing one upload buffer map, write, and unmap it
				// concurrently. Mapping once here leaves the recording pass with nothing
				// but plain memory writes.
				std::vector<wgpu::Buffer> upload_mappings;
				auto release_upload_mappings = [&upload_mappings]() {
					std::ranges::for_each(upload_mappings, [](wgpu::Buffer const& buffer) {
						buffer.Unmap();
					});
					upload_mappings.clear();
				};
				try {
					for (auto const& batch : plan.batches) {
						for (auto const& node : batch.nodes) {
							for (auto const& command : node.commands) {
								auto const* write = std::get_if<WriteBuffer>(&command);
								if (!write) {
									continue;
								}
								auto const* buffer = std::get_if<wgpu::Buffer>(
								    &resources[write->resource].get().impl
								);
								if (!buffer) {
									throw std::invalid_argument(
									    "WebGPU command requires a buffer resource"
									);
								}
								if (!(buffer->GetUsage() & wgpu::BufferUsage::MapWrite)) {
									continue;
								}
								if (buffer->GetMapState() == wgpu::BufferMapState::Mapped) {
									// Already mapped by the caller: leave it mapped, and
									// leave unmapping it to the caller as well.
									continue;
								}
								auto const mapped = std::ranges::any_of(
								    upload_mappings,
								    [buffer](wgpu::Buffer const& candidate) {
									    return candidate.Get() == buffer->Get();
								    }
								);
								if (mapped) {
									continue;
								}
								auto status = wgpu::MapAsyncStatus::Error;
								std::string error;
								auto future = buffer->MapAsync(
								    wgpu::MapMode::Write,
								    0u,
								    wgpu::kWholeMapSize,
								    wgpu::CallbackMode::AllowProcessEvents,
								    [&](wgpu::MapAsyncStatus result, wgpu::StringView message) {
									    status = result;
									    if (message.data && message.length != 0u) {
										    error.assign(message.data, message.length);
									    }
								    }
								);
								// A mapping is a wait-list event, not a queue-serial one, so
								// waiting for it here does not take the device-wide lock that
								// a long queue-serial wait would. It is also serial: this is
								// the preparation phase, before any batch records.
								auto wait_status = context->instance.WaitAny(
								    future,
								    (std::numeric_limits<std::uint64_t>::max)()
								);
								if (wait_status != wgpu::WaitStatus::Success) {
									throw std::runtime_error(
									    "Failed to wait for a WebGPU upload buffer mapping"
									);
								}
								if (status != wgpu::MapAsyncStatus::Success) {
									throw std::runtime_error(
									    error.empty()
									        ? "Failed to map a WebGPU upload buffer"
									        : error
									);
								}
								upload_mappings.emplace_back(*buffer);
							}
						}
					}
				}
				catch (...) {
					release_upload_mappings();
					throw;
				}

				// Phase 2: every batch owns its encoder, command buffer, presentation
				// cursor, and exception slot. The resulting array retains plan order for
				// the single ordered WebGPU queue submission below.
				std::vector<wgpu::CommandBuffer> command_buffers(plan.batches.size());
				std::vector<std::exception_ptr> recording_errors(plan.batches.size());
				std::atomic_bool cancelled = false;
				ParallelFor(std::size_t{0u}, plan.batches.size(), [&](std::size_t batch_index) {
					if (cancelled.load(std::memory_order_acquire) || stop_token.stop_requested()) {
						cancelled.store(true, std::memory_order_release);
						return;
					}
					try {
						auto const& batch = plan.batches[batch_index];
						auto encoder = context->device.CreateCommandEncoder();
						webgpu::Replayer replayer{
						    resources,
						    resource_flags,
						    views,
						    pipelines,
						    resource_groups,
						    presentations,
						    presentation_offsets[batch_index],
						    context->instance,
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
						if (replayer.presentation_cursor !=
						    presentation_offsets[batch_index + 1u]) {
							throw std::logic_error(
							    "WebGPU batch did not consume its prepared presentations"
							);
						}
						command_buffers[batch_index] = encoder.Finish();
					} catch (...) {
						recording_errors[batch_index] = std::current_exception();
					}
				});
				// Every batch has finished recording, so the upload buffers can be
				// flushed and released. They must be unmapped before the queue is asked to
				// execute the recorded work, which is why this is not left to scope exit.
				release_upload_mappings();
				for (auto const& error : recording_errors) {
					if (error) {
						std::rethrow_exception(error);
					}
				}
				if (cancelled.load(std::memory_order_acquire) || stop_token.stop_requested()) {
					{
						std::unique_lock<std::mutex> state_lock(token_state->mutex);
						token_state->stopped.store(true, std::memory_order_release);
						token_state->complete.store(true, std::memory_order_release);
					}
					token_state->condition.notify_all();
					return MakeCompletionToken(
					    webgpu::CompletionToken{context->instance, std::move(token_state), {}}
					);
				}

				auto queue = context->device.GetQueue();
				queue.Submit(command_buffers.size(), command_buffers.data());
				// A submit Dawn rejects is reported only through the device's uncaptured-error
				// callback, which the queue-work future below does not observe: without this the
				// graph would be reported as done even though its work never ran, and the next
				// readback would wait for that work forever.
				if (context->errors && context->errors->Failed()) {
					throw std::runtime_error(
					    std::format("WebGPU submit failed: {}", context->errors->Message())
					);
				}
				for (auto const& work : presentations) {
					auto status = work.surface.Present();
					if (!status) {
						std::unique_lock<std::mutex> state_lock(token_state->mutex);
						if (!token_state->error) {
							token_state->error = std::make_exception_ptr(
							    std::runtime_error("WebGPU surface present failed")
							);
						}
					}
				}
				completion_future = queue.OnSubmittedWorkDone(
				    wgpu::CallbackMode::WaitAnyOnly,
				    [token_state](wgpu::QueueWorkDoneStatus status, wgpu::StringView message) {
					    if (status != wgpu::QueueWorkDoneStatus::Success) {
						    std::unique_lock<std::mutex> state_lock(token_state->mutex);
						    token_state->error = std::make_exception_ptr(
						        std::runtime_error(
						            std::format(
						                "WebGPU queue work failed with status {}: {}",
						                static_cast<int>(status),
						                std::string_view(message.data, message.length)
						            )
						        )
						    );
					    }
					    {
						    std::unique_lock<std::mutex> state_lock(token_state->mutex);
						    token_state->complete.store(true, std::memory_order_release);
					    }
					    token_state->condition.notify_all();
				    }
				);
				// The pump thread is what drives this future: a WaitAnyOnly future only
				// runs its callback inside WaitAny, and the scheduler must never park a
				// thread in a non-zero-timeout wait (that would hold the device-wide lock
				// on the D3D12 backend and stall the next Submit/Present).
				context->Watch(completion_future, token_state);
			} catch (...) {
				{
					std::unique_lock<std::mutex> state_lock(token_state->mutex);
					token_state->error = std::current_exception();
					token_state->complete.store(true, std::memory_order_release);
				}
				token_state->condition.notify_all();
			}
			return MakeCompletionToken(
			    webgpu::CompletionToken{
			        context->instance,
			        std::move(token_state),
			        completion_future
			    }
			);
		}
	};

} // namespace fyuu_rhi::execution
