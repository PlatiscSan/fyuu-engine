module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#endif // !defined(__cpp_lib_modules)
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <Metal/Metal.hpp>
#include <QuartzCore/CAMetalLayer.hpp>
#endif // defined(__APPLE__)

module fyuu_rhi:metal_command_scheduler;
#if defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :command_scheduler_dispatch;
import :command_scheduler_factory;
import :completion_token_factory;
import :execution;
import :logical_device_dispatch;
import :metal_data;
import :pipeline;
import :pipeline_factory;
import :pipeline_resource_group_factory;
import :resource_factory;
import :sampler_factory;
import :view_factory;

namespace {

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::execution;

	MTL::LoadAction NativeLoadAction(LoadOperation operation) noexcept {
		switch (operation) {
		case LoadOperation::Load: return MTL::LoadActionLoad;
		case LoadOperation::Clear: return MTL::LoadActionClear;
		case LoadOperation::Discard: return MTL::LoadActionDontCare;
		default: return MTL::LoadActionDontCare;
		}
	}

	MTL::StoreAction NativeStoreAction(StoreOperation operation) noexcept {
		if (operation == StoreOperation::Store) {
			return MTL::StoreActionStore;
		}
		return MTL::StoreActionDontCare;
	}

	MTL::IndexType NativeIndexType(IndexType type) noexcept {
		if (type == IndexType::Uint16) {
			return MTL::IndexTypeUInt16;
		}
		return MTL::IndexTypeUInt32;
	}

	std::size_t IndexSize(IndexType type) noexcept {
		if (type == IndexType::Uint16) {
			return sizeof(std::uint16_t);
		}
		return sizeof(std::uint32_t);
	}

	bool Visible(std::uint32_t visibility, pipeline::Stage stage) noexcept {
		return (visibility & (1u << static_cast<std::uint32_t>(stage))) != 0u;
	}

	bool HasStencil(MTL::PixelFormat format) noexcept {
		return format == MTL::PixelFormatDepth24Unorm_Stencil8 ||
			format == MTL::PixelFormatDepth32Float_Stencil8;
	}

} // namespace

namespace fyuu_rhi::metal {

	struct PresentationWork {
		std::size_t source;
		NS::SharedPtr<CA::MetalDrawable> drawable;
	};

	struct Replayer {
		std::span<std::reference_wrapper<Resource const> const> resources;
		std::span<std::reference_wrapper<View const> const> views;
		std::span<std::reference_wrapper<Pipeline const> const> pipelines;
		std::span<std::reference_wrapper<PipelineResourceGroup const> const> groups;
		std::span<PresentationWork const> presentations;
		MTL::CommandBuffer* command_buffer;
		std::size_t presentation_cursor = 0u;

		MTL::RenderCommandEncoder* render_encoder = nullptr;
		MTL::ComputeCommandEncoder* compute_encoder = nullptr;
		MTL::BlitCommandEncoder* blit_encoder = nullptr;
		Pipeline const* pipeline = nullptr;
		MTL::Buffer* index_buffer = nullptr;
		std::size_t index_offset = 0u;
		IndexType index_type = IndexType::Uint16;

		struct VertexBufferBinding {
			std::uint32_t slot;
			MTL::Buffer* buffer;
			std::size_t offset;
		};

		std::vector<VertexBufferBinding> pending_vertex_buffers;
		std::vector<PipelineResourceGroup const*> pending_groups;
		std::optional<Viewport> pending_viewport;
		std::optional<Scissor> pending_scissor;

		MTL::Buffer* BufferAt(std::size_t index) const {
			auto buffer = std::get_if<NS::SharedPtr<MTL::Buffer>>(
				&resources[index].get().impl
			);
			if (!buffer || !*buffer) {
				throw std::invalid_argument("Metal command requires a buffer resource");
			}
			return buffer->get();
		}

		MTL::Texture* TextureAt(std::size_t index) const {
			auto texture = std::get_if<NS::SharedPtr<MTL::Texture>>(
				&resources[index].get().impl
			);
			if (!texture || !*texture) {
				throw std::invalid_argument("Metal command requires a texture resource");
			}
			return texture->get();
		}

		MTL::Texture* TextureViewAt(std::size_t index) const {
			auto texture = std::get_if<NS::SharedPtr<MTL::Texture>>(
				&views[index].get().impl
			);
			if (!texture || !*texture) {
				throw std::invalid_argument("Metal command requires a texture view");
			}
			return texture->get();
		}

		void EndEncoders() {
			if (render_encoder) {
				render_encoder->endEncoding();
				render_encoder = nullptr;
			}
			if (compute_encoder) {
				compute_encoder->endEncoding();
				compute_encoder = nullptr;
			}
			if (blit_encoder) {
				blit_encoder->endEncoding();
				blit_encoder = nullptr;
			}
		}

		MTL::BlitCommandEncoder* BeginBlit() {
			if (blit_encoder) {
				return blit_encoder;
			}
			EndEncoders();
			blit_encoder = command_buffer->blitCommandEncoder();
			if (!blit_encoder) {
				throw std::runtime_error("Metal failed to create a blit encoder");
			}
			return blit_encoder;
		}

		void BindGroup(PipelineResourceGroup const& group) {
			std::ranges::for_each(
				group.bindings,
				[&](auto const& binding) {
					if (render_encoder) {
						if (Visible(binding.visibility, pipeline::Stage::Vertex)) {
							if (binding.buffer) {
								render_encoder->setVertexBuffer(
									binding.buffer.get(),
									binding.buffer_offset,
									binding.slot
								);
							}
							if (binding.texture) {
								render_encoder->setVertexTexture(
									binding.texture.get(),
									binding.slot
								);
							}
							if (binding.sampler) {
								render_encoder->setVertexSamplerState(
									binding.sampler.get(),
									binding.slot
								);
							}
						}
						if (Visible(binding.visibility, pipeline::Stage::Fragment)) {
							if (binding.buffer) {
								render_encoder->setFragmentBuffer(
									binding.buffer.get(),
									binding.buffer_offset,
									binding.slot
								);
							}
							if (binding.texture) {
								render_encoder->setFragmentTexture(
									binding.texture.get(),
									binding.slot
								);
							}
							if (binding.sampler) {
								render_encoder->setFragmentSamplerState(
									binding.sampler.get(),
									binding.slot
								);
							}
						}
					}
					else if (compute_encoder) {
						if (binding.buffer) {
							compute_encoder->setBuffer(
								binding.buffer.get(),
								binding.buffer_offset,
								binding.slot
							);
						}
						if (binding.texture) {
							compute_encoder->setTexture(
								binding.texture.get(),
								binding.slot
							);
						}
						if (binding.sampler) {
							compute_encoder->setSamplerState(
								binding.sampler.get(),
								binding.slot
							);
						}
					}
				}
			);
		}

		void FlushRenderState() {
			if (!render_encoder) {
				return;
			}
			if (pipeline) {
				auto state = std::get_if<NS::SharedPtr<MTL::RenderPipelineState>>(
					&pipeline->impl
				);
				if (state && *state) {
					render_encoder->setRenderPipelineState(state->get());
					render_encoder->setDepthStencilState(pipeline->depth_stencil.get());
					render_encoder->setFrontFacingWinding(pipeline->front_face);
					render_encoder->setCullMode(pipeline->cull_mode);
					render_encoder->setDepthBias(
						static_cast<float>(pipeline->depth_bias.constant),
						pipeline->depth_bias.slope_scale,
						pipeline->depth_bias.clamp
					);
				}
			}
			std::ranges::for_each(
				pending_vertex_buffers,
				[&](auto const& binding) {
					render_encoder->setVertexBuffer(
						binding.buffer,
						binding.offset,
						binding.slot
					);
				}
			);
			if (pending_viewport) {
				operator()(*pending_viewport);
			}
			if (pending_scissor) {
				operator()(*pending_scissor);
			}
			std::ranges::for_each(
				pending_groups,
				[this](auto group) {
					BindGroup(*group);
				}
			);
		}

		void operator()(BeginRendering const& value) {
			EndEncoders();
			auto descriptor = MTL::RenderPassDescriptor::renderPassDescriptor();
			std::ranges::for_each(
				std::views::iota(std::size_t{ 0u }, value.colors.size()),
				[&](std::size_t index) {
					auto const& source = value.colors[index];
					auto attachment = descriptor->colorAttachments()->object(index);
					attachment->setTexture(TextureViewAt(source.view));
					attachment->setLoadAction(NativeLoadAction(source.load));
					attachment->setStoreAction(NativeStoreAction(source.store));
					attachment->setClearColor(
						MTL::ClearColor{
							source.clear.red,
							source.clear.green,
							source.clear.blue,
							source.clear.alpha
						}
					);
					if (source.resolve_view) {
						attachment->setResolveTexture(TextureViewAt(*source.resolve_view));
						if (source.store == StoreOperation::Store) {
							attachment->setStoreAction(
								MTL::StoreActionStoreAndMultisampleResolve
							);
						}
						else {
							attachment->setStoreAction(MTL::StoreActionMultisampleResolve);
						}
					}
				}
			);
			if (value.depth_stencil) {
				auto const& source = *value.depth_stencil;
				auto texture = TextureViewAt(source.view);
				auto depth = descriptor->depthAttachment();
				depth->setTexture(texture);
				depth->setLoadAction(NativeLoadAction(source.depth_load));
				depth->setStoreAction(NativeStoreAction(source.depth_store));
				depth->setClearDepth(source.clear_depth);
				if (HasStencil(texture->pixelFormat())) {
					auto stencil = descriptor->stencilAttachment();
					stencil->setTexture(texture);
					stencil->setLoadAction(NativeLoadAction(source.stencil_load));
					stencil->setStoreAction(NativeStoreAction(source.stencil_store));
					stencil->setClearStencil(source.clear_stencil);
				}
			}
			render_encoder = command_buffer->renderCommandEncoder(descriptor);
			if (!render_encoder) {
				throw std::runtime_error("Metal failed to create a render encoder");
			}
			render_encoder->setViewport(
				MTL::Viewport{
					static_cast<double>(value.area.x),
					static_cast<double>(value.area.y),
					static_cast<double>(value.area.width),
					static_cast<double>(value.area.height),
					0.0,
					1.0
				}
			);
			render_encoder->setScissorRect(
				MTL::ScissorRect{
					static_cast<NS::UInteger>((std::max)(value.area.x, 0)),
					static_cast<NS::UInteger>((std::max)(value.area.y, 0)),
					value.area.width,
					value.area.height
				}
			);
			FlushRenderState();
		}

		void operator()(EndRendering const&) {
			if (!render_encoder) {
				throw std::logic_error("Metal EndRendering without BeginRendering");
			}
			render_encoder->endEncoding();
			render_encoder = nullptr;
		}

		void operator()(BindPipeline const& value) {
			pipeline = &pipelines[value.pipeline].get();
			if (auto render = std::get_if<NS::SharedPtr<MTL::RenderPipelineState>>(
				&pipeline->impl
			)) {
				if (render_encoder) {
					render_encoder->setRenderPipelineState(render->get());
					render_encoder->setDepthStencilState(pipeline->depth_stencil.get());
					render_encoder->setFrontFacingWinding(pipeline->front_face);
					render_encoder->setCullMode(pipeline->cull_mode);
				}
				return;
			}
			if (compute_encoder) {
				compute_encoder->setComputePipelineState(
					std::get<NS::SharedPtr<MTL::ComputePipelineState>>(pipeline->impl).get()
				);
			}
		}

		void operator()(BindResourceGroup const& value) {
			auto const& group = groups[value.group].get();
			if (group.space != value.index) {
				throw std::invalid_argument("Metal resource group space does not match bind index");
			}
			if (render_encoder || compute_encoder) {
				BindGroup(group);
			}
			else {
				pending_groups.emplace_back(&group);
			}
		}

		void operator()(BindVertexBuffer const& value) {
			auto buffer = BufferAt(value.resource);
			if (render_encoder) {
				render_encoder->setVertexBuffer(buffer, value.offset, value.slot);
			}
			else {
				pending_vertex_buffers.emplace_back(
					VertexBufferBinding{ value.slot, buffer, value.offset }
				);
			}
		}

		void operator()(BindIndexBuffer const& value) {
			index_buffer = BufferAt(value.resource);
			index_offset = value.offset;
			index_type = value.type;
		}

		void operator()(Viewport const& value) {
			if (!render_encoder) {
				pending_viewport = value;
				return;
			}
			render_encoder->setViewport(
				MTL::Viewport{
					value.x,
					value.y,
					value.width,
					value.height,
					value.minimum_depth,
					value.maximum_depth
				}
			);
		}

		void operator()(Scissor const& value) {
			if (!render_encoder) {
				pending_scissor = value;
				return;
			}
			render_encoder->setScissorRect(
				MTL::ScissorRect{
					static_cast<NS::UInteger>((std::max)(value.x, 0)),
					static_cast<NS::UInteger>((std::max)(value.y, 0)),
					value.width,
					value.height
				}
			);
		}

		void operator()(Draw const& value) {
			if (!render_encoder || !pipeline) {
				throw std::logic_error("Metal draw requires an active render pipeline");
			}
			render_encoder->drawPrimitives(
				pipeline->primitive_type,
				value.first_vertex,
				value.vertex_count,
				value.instance_count,
				value.first_instance
			);
		}

		void operator()(DrawIndexed const& value) {
			if (!render_encoder || !pipeline || !index_buffer) {
				throw std::logic_error(
					"Metal indexed draw requires a pipeline and index buffer"
				);
			}
			render_encoder->drawIndexedPrimitives(
				pipeline->primitive_type,
				value.index_count,
				NativeIndexType(index_type),
				index_buffer,
				index_offset + value.first_index * IndexSize(index_type),
				value.instance_count,
				value.vertex_offset,
				value.first_instance
			);
		}

		void operator()(Dispatch const& value) {
			if (!pipeline) {
				throw std::logic_error("Metal dispatch requires a compute pipeline");
			}
			auto state = std::get_if<NS::SharedPtr<MTL::ComputePipelineState>>(
				&pipeline->impl
			);
			if (!state || !*state) {
				throw std::logic_error("Metal dispatch requires a compute pipeline");
			}
			if (!compute_encoder) {
				EndEncoders();
				compute_encoder = command_buffer->computeCommandEncoder();
				if (!compute_encoder) {
					throw std::runtime_error("Metal failed to create a compute encoder");
				}
				compute_encoder->setComputePipelineState(state->get());
				std::ranges::for_each(
					pending_groups,
					[this](auto group) {
						BindGroup(*group);
					}
				);
			}
			auto width = state->get()->threadExecutionWidth();
			compute_encoder->dispatchThreadgroups(
				MTL::Size{
					value.group_count_x,
					value.group_count_y,
					value.group_count_z
				},
				MTL::Size{ width, 1u, 1u }
			);
		}

		void operator()(CopyBuffer const& value) {
			BeginBlit()->copyFromBuffer(
				BufferAt(value.source),
				value.source_offset,
				BufferAt(value.destination),
				value.destination_offset,
				value.size
			);
		}

		void operator()(CopyBufferToTexture const& value) {
			auto texture = TextureAt(value.destination);
			auto is_3d = texture->textureType() == MTL::TextureType3D;
			auto layers = is_3d ? 1u : value.destination_region.depth;
			for (std::uint32_t layer = 0u; layer < layers; ++layer) {
				BeginBlit()->copyFromBuffer(
					BufferAt(value.source),
					value.source_layout.offset +
						layer * value.source_layout.bytes_per_row *
						value.source_layout.rows_per_image,
					value.source_layout.bytes_per_row,
					value.source_layout.bytes_per_row * value.source_layout.rows_per_image,
					MTL::Size{
						value.destination_region.width,
						value.destination_region.height,
						is_3d ? value.destination_region.depth : 1u
					},
					texture,
					is_3d ? 0u : value.destination_region.offset_z + layer,
					value.destination_region.mip_level,
					MTL::Origin{
						value.destination_region.offset_x,
						value.destination_region.offset_y,
						is_3d ? value.destination_region.offset_z : 0u
					}
				);
			}
		}

		void operator()(CopyTextureToBuffer const& value) {
			auto texture = TextureAt(value.source);
			auto is_3d = texture->textureType() == MTL::TextureType3D;
			auto layers = is_3d ? 1u : value.source_region.depth;
			for (std::uint32_t layer = 0u; layer < layers; ++layer) {
				BeginBlit()->copyFromTexture(
					texture,
					is_3d ? 0u : value.source_region.offset_z + layer,
					value.source_region.mip_level,
					MTL::Origin{
						value.source_region.offset_x,
						value.source_region.offset_y,
						is_3d ? value.source_region.offset_z : 0u
					},
					MTL::Size{
						value.source_region.width,
						value.source_region.height,
						is_3d ? value.source_region.depth : 1u
					},
					BufferAt(value.destination),
					value.destination_layout.offset +
						layer * value.destination_layout.bytes_per_row *
						value.destination_layout.rows_per_image,
					value.destination_layout.bytes_per_row,
					value.destination_layout.bytes_per_row *
						value.destination_layout.rows_per_image
				);
			}
		}

		void operator()(CopyTexture const& value) {
			auto source = TextureAt(value.source);
			auto destination = TextureAt(value.destination);
			auto source_3d = source->textureType() == MTL::TextureType3D;
			auto destination_3d = destination->textureType() == MTL::TextureType3D;
			if (source_3d != destination_3d) {
				throw std::invalid_argument(
					"Metal cannot directly copy between 3D and array textures"
				);
			}
			auto layers = source_3d ? 1u : value.source_region.depth;
			for (std::uint32_t layer = 0u; layer < layers; ++layer) {
				BeginBlit()->copyFromTexture(
					source,
					source_3d ? 0u : value.source_region.offset_z + layer,
					value.source_region.mip_level,
					MTL::Origin{
						value.source_region.offset_x,
						value.source_region.offset_y,
						source_3d ? value.source_region.offset_z : 0u
					},
					MTL::Size{
						value.source_region.width,
						value.source_region.height,
						source_3d ? value.source_region.depth : 1u
					},
					destination,
					destination_3d ? 0u : value.destination_region.offset_z + layer,
					value.destination_region.mip_level,
					MTL::Origin{
						value.destination_region.offset_x,
						value.destination_region.offset_y,
						destination_3d ? value.destination_region.offset_z : 0u
					}
				);
			}
		}

		void operator()(WriteBuffer const& value) {
			auto buffer = BufferAt(value.resource);
			auto length = static_cast<std::size_t>(buffer->length());
			if (value.offset > length || value.data.size() > length - value.offset) {
				throw std::out_of_range("Metal write exceeds the buffer size");
			}
			if (value.data.empty()) {
				return;
			}
			auto upload = NS::TransferPtr(
				buffer->device()->newBuffer(
					value.data.data(),
					value.data.size(),
					MTL::ResourceStorageModeShared
				)
			);
			if (!upload) {
				throw std::bad_alloc();
			}
			BeginBlit()->copyFromBuffer(
				upload.get(),
				0u,
				buffer,
				value.offset,
				value.data.size()
			);
		}

		void operator()(Present const& value) {
			if (presentation_cursor >= presentations.size()) {
				throw std::logic_error("Metal presentation work was not prepared");
			}
			auto const& work = presentations[presentation_cursor++];
			if (work.source != value.source) {
				throw std::logic_error("Metal presentation work does not match its command");
			}
			EndEncoders();
			BeginBlit()->copyFromTexture(
				TextureAt(value.source),
				work.drawable->texture()
			);
			EndEncoders();
			command_buffer->presentDrawable(work.drawable.get());
		}
	};

} // namespace fyuu_rhi::metal

namespace fyuu_rhi {

	template <>
	struct CreateScheduler<metal::LogicalDevice> {
		metal::LogicalDevice* logical_device;

		execution::CommandScheduler operator()() const {
			auto queue = NS::TransferPtr(logical_device->impl->newCommandQueue());
			if (!queue) {
				throw std::runtime_error("Failed to create the Metal command queue");
			}
			return execution::MakeCommandScheduler(
				metal::CommandSchedulerContext{
					logical_device->impl,
					std::move(queue)
				}
			);
		}
	};

} // namespace fyuu_rhi

namespace fyuu_rhi::execution {

	template <>
	struct ExecuteCommands<metal::CommandSchedulerContext> {
		metal::CommandSchedulerContext* context;

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
			auto autorelease_pool = NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
			std::vector<std::reference_wrapper<metal::Resource const>> resources;
			std::vector<std::reference_wrapper<metal::View const>> views;
			std::vector<std::reference_wrapper<metal::Sampler const>> samplers;
			std::vector<std::reference_wrapper<metal::Pipeline const>> pipelines;
			std::vector<std::reference_wrapper<metal::PipelineResourceGroup const>> groups;
			resources.reserve(bound_resources.size());
			views.reserve(bound_views.size());
			samplers.reserve(bound_samplers.size());
			pipelines.reserve(bound_pipelines.size());
			groups.reserve(bound_resource_groups.size());

			std::ranges::transform(
				bound_resources,
				std::back_inserter(resources),
				[](Resource const& resource) -> metal::Resource const& {
					if (!resource.m_impl) {
						throw std::invalid_argument("A Metal execution resource is empty");
					}
					auto native = std::get_if<metal::Resource>(&resource.m_impl->native);
					if (!native) {
						throw std::invalid_argument(
							"A Metal execution resource uses another backend"
						);
					}
					return *native;
				}
			);
			std::ranges::transform(
				bound_views,
				std::back_inserter(views),
				[](View const& view) -> metal::View const& {
					if (!view.m_impl) {
						throw std::invalid_argument("A Metal execution view is empty");
					}
					auto native = std::get_if<metal::View>(&view.m_impl->native);
					if (!native) {
						throw std::invalid_argument(
							"A Metal execution view uses another backend"
						);
					}
					return *native;
				}
			);
			std::ranges::transform(
				bound_samplers,
				std::back_inserter(samplers),
				[](Sampler const& sampler) -> metal::Sampler const& {
					if (!sampler.m_impl) {
						throw std::invalid_argument("A Metal execution sampler is empty");
					}
					auto native = std::get_if<metal::Sampler>(&sampler.m_impl->native);
					if (!native) {
						throw std::invalid_argument(
							"A Metal execution sampler uses another backend"
						);
					}
					return *native;
				}
			);
			std::ranges::transform(
				bound_pipelines,
				std::back_inserter(pipelines),
				[](Pipeline const& pipeline) -> metal::Pipeline const& {
					if (!pipeline.m_impl) {
						throw std::invalid_argument("A Metal execution pipeline is empty");
					}
					auto native = std::get_if<metal::Pipeline>(&pipeline.m_impl->native);
					if (!native) {
						throw std::invalid_argument(
							"A Metal execution pipeline uses another backend"
						);
					}
					return *native;
				}
			);
			std::ranges::transform(
				bound_resource_groups,
				std::back_inserter(groups),
				[](PipelineResourceGroup const& group) -> metal::PipelineResourceGroup const& {
					if (!group.m_impl) {
						throw std::invalid_argument(
							"A Metal execution resource group is empty"
						);
					}
					auto native = std::get_if<metal::PipelineResourceGroup>(
						&group.m_impl->native
					);
					if (!native) {
						throw std::invalid_argument(
							"A Metal execution resource group uses another backend"
						);
					}
					return *native;
				}
			);

			if (
				resources.size() != plan.bindings.resource_count ||
				views.size() != plan.bindings.view_count ||
				samplers.size() != plan.bindings.sampler_count ||
				pipelines.size() != plan.bindings.pipeline_count ||
				groups.size() != plan.bindings.resource_group_count
			) {
				throw std::invalid_argument("Metal execution binding count mismatch");
			}
			std::vector<std::vector<metal::PresentationWork>> presentations(
				plan.batches.size()
			);
			std::vector<PlatformHandle> active_targets;
			for (std::size_t batch_index = 0u; batch_index < plan.batches.size(); ++batch_index) {
				auto const& batch = plan.batches[batch_index];
				if (batch.id != batch_index) {
					throw std::invalid_argument(
						"Metal execution batch IDs must match storage indices"
					);
				}
				std::ranges::for_each(
					batch.dependencies,
					[batch_index](std::size_t dependency) {
						if (dependency >= batch_index) {
							throw std::invalid_argument(
								"Metal execution batches are not topologically ordered"
							);
						}
					}
				);
				for (auto const& node : batch.nodes) {
					for (auto const& command : node.commands) {
						auto present = std::get_if<Present>(&command);
						if (!present) {
							continue;
						}
						if (
							present->target >= presentation_targets.size() ||
							present->source >= resources.size()
						) {
							throw std::invalid_argument("Metal presentation binding is invalid");
						}
						auto layer = presentation_targets[present->target];
						if (!layer) {
							throw std::invalid_argument("Metal presentation target is null");
						}
						if (std::ranges::find(active_targets, layer) != active_targets.end()) {
							throw std::invalid_argument(
								"Metal execution cannot present one target more than once"
							);
						}
						active_targets.emplace_back(layer);
						if (layer->device() != context->device.get()) {
							layer->setDevice(context->device.get());
						}
						if (present->buffer_count < 2u || present->buffer_count > 3u) {
							throw std::invalid_argument(
								"Metal presentation buffer count must be two or three"
							);
						}
						auto source = std::get_if<NS::SharedPtr<MTL::Texture>>(
							&resources[present->source].get().impl
						);
						if (!source || !*source) {
							throw std::invalid_argument(
								"Metal presentation source must be a texture"
							);
						}
						layer->setPixelFormat((*source)->pixelFormat());
						layer->setDrawableSize(
							CGSize{
								static_cast<CGFloat>((*source)->width()),
								static_cast<CGFloat>((*source)->height())
							}
						);
						layer->setFramebufferOnly(false);
						layer->setMaximumDrawableCount(present->buffer_count);
#if TARGET_OS_OSX
						layer->setDisplaySyncEnabled(present->vertical_sync);
#endif // TARGET_OS_OSX
						auto drawable = NS::RetainPtr(layer->nextDrawable());
						if (!drawable) {
							throw std::runtime_error("Metal failed to acquire the next drawable");
						}
						presentations[batch_index].emplace_back(
							metal::PresentationWork{
								present->source,
								std::move(drawable)
							}
						);
					}
				}
			}

			if (stop_token.stop_requested()) {
				return MakeCompletionToken(
					metal::CompletionToken{ {}, {}, true }
				);
			}

			std::vector<NS::SharedPtr<MTL::CommandBuffer>> command_buffers;
			std::exception_ptr error;
			try {
				command_buffers.reserve(plan.batches.size());
				for (std::size_t batch_index = 0u; batch_index < plan.batches.size(); ++batch_index) {
					if (stop_token.stop_requested()) {
						return MakeCompletionToken(
							metal::CompletionToken{ {}, {}, true }
						);
					}
					auto command_buffer = NS::RetainPtr(context->queue->commandBuffer());
					if (!command_buffer) {
						throw std::runtime_error("Metal failed to create a command buffer");
					}
					metal::Replayer replayer{
						resources,
						views,
						pipelines,
						groups,
						presentations[batch_index],
						command_buffer.get()
					};
					for (auto const& node : plan.batches[batch_index].nodes) {
						for (auto const& command : node.commands) {
							std::visit(replayer, command);
						}
					}
					replayer.EndEncoders();
					if (replayer.presentation_cursor != presentations[batch_index].size()) {
						throw std::logic_error(
							"Metal batch did not consume every prepared presentation"
						);
					}
					command_buffers.emplace_back(std::move(command_buffer));
				}

				std::ranges::for_each(
					command_buffers,
					[](auto const& command_buffer) {
						command_buffer->commit();
					}
				);
			}
			catch (...) {
				error = std::current_exception();
				command_buffers.clear();
			}
			return MakeCompletionToken(
				metal::CompletionToken{
					std::move(command_buffers),
					std::move(error),
					false
				}
			);
		}
	};

} // namespace fyuu_rhi::execution
#endif // defined(__APPLE__)
