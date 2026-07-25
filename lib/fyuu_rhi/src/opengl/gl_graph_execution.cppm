module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <array>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <stop_token>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#include "glad/glad.h"
#if defined(_WIN32)
#include <Windows.h>
#include "glad/glad_wgl.h"
#elif defined(__linux__)
#include "glad/glad_egl.h"
#include "glad/glad_glx.h"
#elif defined(__ANDROID__)
#include "glad/glad_egl.h"
#endif
#endif // !defined(__APPLE__)

module fyuu_rhi:opengl_graph_execution;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :opengl_traits;

namespace {

	bool PollCompleted() noexcept {
		return true;
	}

	struct CreateCommandList {
		std::vector<fyuu_rhi::execution::GraphCommand> operator()() const {
			return {};
		}
	};

	struct ResetCommandList {
		void operator()(std::vector<fyuu_rhi::execution::GraphCommand>& commands) const {
			commands.clear();
		}
	};

	GLbitfield OpenGLBarrierBits(fyuu_rhi::execution::GraphAccessFlagBits access) noexcept {
		using Flag = fyuu_rhi::execution::GraphAccessFlagBits;
		GLbitfield result = 0u;
		if ((access & Flag::Vertex) != Flag::None) result |= GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT;
		if ((access & Flag::Index) != Flag::None) result |= GL_ELEMENT_ARRAY_BARRIER_BIT;
		if ((access & Flag::Uniform) != Flag::None) result |= GL_UNIFORM_BARRIER_BIT;
		if ((access & Flag::Storage) != Flag::None) {
			result |= GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
		}
		if ((access & Flag::Sampled) != Flag::None) result |= GL_TEXTURE_FETCH_BARRIER_BIT;
		if ((access & Flag::Indirect) != Flag::None) result |= GL_COMMAND_BARRIER_BIT;
		if ((access & (Flag::CopySource | Flag::CopyDestination)) != Flag::None) {
			result |= GL_BUFFER_UPDATE_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT;
		}
		if ((access & (Flag::ColorAttachment | Flag::DepthStencilAttachment |
			Flag::ResolveSource | Flag::ResolveDestination | Flag::Present)) != Flag::None) {
			result |= GL_FRAMEBUFFER_BARRIER_BIT;
		}
		return result;
	}

	GLenum OpenGLPrimitive(fyuu_rhi::pipeline::PrimitiveTopology topology) {
		using Topology = fyuu_rhi::pipeline::PrimitiveTopology;
		switch (topology) {
		case Topology::PointList: return GL_POINTS;
		case Topology::LineList: return GL_LINES;
		case Topology::LineStrip: return GL_LINE_STRIP;
		case Topology::TriangleList: return GL_TRIANGLES;
		case Topology::TriangleStrip: return GL_TRIANGLE_STRIP;
		default: throw std::invalid_argument("Unsupported OpenGL primitive topology");
		}
	}

	GLenum OpenGLAttachment(GLenum format) noexcept {
		return format == GL_DEPTH24_STENCIL8 || format == GL_DEPTH32F_STENCIL8
			? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT;
	}

	struct OpenGLVertexFormat {
		GLint components;
		GLenum type;
		GLboolean normalized;
		bool integer;
	};

	OpenGLVertexFormat GetOpenGLVertexFormat(fyuu_rhi::ResourceFlagBits format) {
		using Format = fyuu_rhi::ResourceFlagBits;
		switch (format) {
		case Format::R8Unorm: return { 1, GL_UNSIGNED_BYTE, GL_TRUE, false };
		case Format::R8Snorm: return { 1, GL_BYTE, GL_TRUE, false };
		case Format::R8Uint: return { 1, GL_UNSIGNED_BYTE, GL_FALSE, true };
		case Format::R8Sint: return { 1, GL_BYTE, GL_FALSE, true };
		case Format::R8G8Unorm: return { 2, GL_UNSIGNED_BYTE, GL_TRUE, false };
		case Format::R8G8Snorm: return { 2, GL_BYTE, GL_TRUE, false };
		case Format::R8G8Uint: return { 2, GL_UNSIGNED_BYTE, GL_FALSE, true };
		case Format::R8G8Sint: return { 2, GL_BYTE, GL_FALSE, true };
		case Format::R8G8B8A8Unorm: return { 4, GL_UNSIGNED_BYTE, GL_TRUE, false };
		case Format::R8G8B8A8Snorm: return { 4, GL_BYTE, GL_TRUE, false };
		case Format::R8G8B8A8Uint: return { 4, GL_UNSIGNED_BYTE, GL_FALSE, true };
		case Format::R8G8B8A8Sint: return { 4, GL_BYTE, GL_FALSE, true };
		case Format::R16Float: return { 1, GL_HALF_FLOAT, GL_FALSE, false };
		case Format::R16G16Float: return { 2, GL_HALF_FLOAT, GL_FALSE, false };
		case Format::R16G16B16A16Float: return { 4, GL_HALF_FLOAT, GL_FALSE, false };
		case Format::R32Float: return { 1, GL_FLOAT, GL_FALSE, false };
		case Format::R32G32Float: return { 2, GL_FLOAT, GL_FALSE, false };
		case Format::R32G32B32A32Float: return { 4, GL_FLOAT, GL_FALSE, false };
		case Format::R32Uint: return { 1, GL_UNSIGNED_INT, GL_FALSE, true };
		case Format::R32G32Uint: return { 2, GL_UNSIGNED_INT, GL_FALSE, true };
		case Format::R32G32B32A32Uint: return { 4, GL_UNSIGNED_INT, GL_FALSE, true };
		case Format::R32Sint: return { 1, GL_INT, GL_FALSE, true };
		case Format::R32G32Sint: return { 2, GL_INT, GL_FALSE, true };
		case Format::R32G32B32A32Sint: return { 4, GL_INT, GL_FALSE, true };
		default: throw std::invalid_argument("Unsupported OpenGL vertex format");
		}
	}

}

namespace fyuu_rhi::opengl {
	namespace {
		using Bindings = execution::NativeCommandGraphBindings<Backend>;

		pipeline::PipelineBindingMetadata const& FindBinding(
			Backend::PipelineResourceGroup const& group,
			std::uint32_t slot
		) {
			auto MatchesBinding = [slot](pipeline::PipelineBindingMetadata const& value) {
				return value.slot == slot;
			};
			auto result = std::ranges::find_if(group.layout, MatchesBinding);
			if (result == group.layout.end()) {
				throw std::invalid_argument("OpenGL resource group contains an unknown binding");
			}
			return *result;
		}

		void BindTexture(std::uint32_t unit, Backend::View const& view) {
			auto texture = std::get_if<Backend::GLTextureView>(&view.impl);
			if (!texture) {
				auto buffer = std::get_if<Backend::GLBufferView>(&view.impl);
				if (!buffer) throw std::invalid_argument("OpenGL texture binding requires a view");
				glActiveTexture(GL_TEXTURE0 + unit);
				glBindTexture(GL_TEXTURE_BUFFER, buffer->impl);
				return;
			}
			glActiveTexture(GL_TEXTURE0 + unit);
			glBindTexture(texture->target, texture->impl);
		}

		void AttachView(GLenum attachment, Backend::View const& view) {
			auto texture = std::get_if<Backend::GLTextureView>(&view.impl);
			if (!texture) throw std::invalid_argument("OpenGL attachment requires a texture view");
			switch (texture->target) {
			case GL_TEXTURE_2D:
			case GL_TEXTURE_CUBE_MAP:
				glFramebufferTexture2D(
					GL_FRAMEBUFFER, attachment, texture->target, texture->impl, 0
				);
				break;
			default:
				glFramebufferTextureLayer(GL_FRAMEBUFFER, attachment, texture->impl, 0, 0);
				break;
			}
		}

		struct OpenGLCommandRecorder {
			Bindings const* bindings;
			Backend::LogicalDevice const* logical_device;
			Backend::Pipeline const* current_pipeline = nullptr;
			GLuint framebuffer = 0u;
			GLuint vertex_array = 0u;
			GLuint index_buffer = 0u;
			std::size_t index_offset = 0u;
			bool index_uint32 = false;
			std::optional<execution::BeginRenderingCommand> rendering;

			void operator()(execution::BeginRenderingCommand const& command) {
				if (rendering) throw std::logic_error("Nested OpenGL rendering scopes are invalid");
				glGenFramebuffers(1u, &framebuffer);
				glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
				std::vector<GLenum> draw_buffers;
				draw_buffers.reserve(command.colors.size());
				for (std::size_t index = 0u; index < command.colors.size(); ++index) {
					auto attachment = GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index);
					AttachView(attachment, bindings->views[command.colors[index].view.value].get());
					draw_buffers.emplace_back(attachment);
					if (!command.colors[index].load) {
						std::array clear{
							command.colors[index].clear_red, command.colors[index].clear_green,
							command.colors[index].clear_blue, command.colors[index].clear_alpha
						};
						glClearBufferfv(GL_COLOR, static_cast<GLint>(index), clear.data());
					}
				}
				if (!draw_buffers.empty()) {
					glDrawBuffers(static_cast<GLsizei>(draw_buffers.size()), draw_buffers.data());
				}
				if (command.depth_stencil) {
					auto const& attachment = *command.depth_stencil;
					auto const& view = bindings->views[attachment.view.value].get();
					auto const& texture = std::get<Backend::GLTextureView>(view.impl);
					AttachView(OpenGLAttachment(texture.format), view);
					if (!attachment.load_depth && !attachment.load_stencil) {
						glClearBufferfi(
							GL_DEPTH_STENCIL, 0, attachment.clear_depth,
							static_cast<GLint>(attachment.clear_stencil)
						);
					}
					else if (!attachment.load_depth) {
						glClearBufferfv(GL_DEPTH, 0, &attachment.clear_depth);
					}
					else if (!attachment.load_stencil) {
						auto stencil = static_cast<GLint>(attachment.clear_stencil);
						glClearBufferiv(GL_STENCIL, 0, &stencil);
					}
				}
				if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
					throw std::runtime_error("OpenGL rendering framebuffer is incomplete");
				}
				glViewport(command.offset_x, command.offset_y, command.width, command.height);
				rendering = command;
			}

			void operator()(execution::EndRenderingCommand const&) {
				if (!rendering) throw std::logic_error("OpenGL rendering scope is not active");
				for (std::size_t index = 0u; index < rendering->colors.size(); ++index) {
					auto const& color = rendering->colors[index];
					if (!color.resolve_view) continue;
					GLuint resolve = 0u;
					glGenFramebuffers(1u, &resolve);
					glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve);
					AttachView(GL_COLOR_ATTACHMENT0, bindings->views[color.resolve_view->value].get());
					glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
					glReadBuffer(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index));
					glBlitFramebuffer(
						rendering->offset_x, rendering->offset_y,
						rendering->offset_x + static_cast<GLint>(rendering->width),
						rendering->offset_y + static_cast<GLint>(rendering->height),
						0, 0, rendering->width, rendering->height, GL_COLOR_BUFFER_BIT, GL_NEAREST
					);
					glDeleteFramebuffers(1u, &resolve);
				}
				glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
				std::vector<GLenum> invalidated;
				for (std::size_t index = 0u; index < rendering->colors.size(); ++index) {
					if (!rendering->colors[index].store) {
						invalidated.emplace_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index));
					}
				}
				if (rendering->depth_stencil && !rendering->depth_stencil->store_depth) {
					invalidated.emplace_back(GL_DEPTH_ATTACHMENT);
				}
				if (rendering->depth_stencil && !rendering->depth_stencil->store_stencil) {
					invalidated.emplace_back(GL_STENCIL_ATTACHMENT);
				}
				if (!invalidated.empty()) {
					glInvalidateFramebuffer(
						GL_FRAMEBUFFER, static_cast<GLsizei>(invalidated.size()), invalidated.data()
					);
				}
				glBindFramebuffer(GL_FRAMEBUFFER, 0u);
				glDeleteFramebuffers(1u, &framebuffer);
				framebuffer = 0u;
				rendering.reset();
			}

			void operator()(execution::BindPipelineCommand const& command) {
				current_pipeline = &bindings->pipelines[command.pipeline.value].get();
				if (current_pipeline->compute && rendering) {
					throw std::logic_error("OpenGL compute pipeline cannot be bound while rendering");
				}
				glUseProgram(current_pipeline->impl);
				if (!current_pipeline->compute) {
					glBindVertexArray(vertex_array);
					for (auto const& attribute : current_pipeline->vertex_attributes) {
						auto format = GetOpenGLVertexFormat(attribute.format);
						glEnableVertexAttribArray(attribute.location);
						glVertexAttribBinding(attribute.location, attribute.slot);
						if (format.integer) {
							glVertexAttribIFormat(
								attribute.location, format.components, format.type, attribute.offset
							);
						}
						else {
							glVertexAttribFormat(
								attribute.location, format.components, format.type,
								format.normalized, attribute.offset
							);
						}
					}
					for (auto const& buffer : current_pipeline->vertex_buffers) {
						glVertexBindingDivisor(
							buffer.slot,
							buffer.input_rate == pipeline::VertexInputRate::Instance ? 1u : 0u
						);
					}
					glFrontFace(current_pipeline->rasterization.front_face == pipeline::FrontFace::Clockwise
						? GL_CW : GL_CCW);
					if (current_pipeline->rasterization.cull_mode == pipeline::CullMode::None) glDisable(GL_CULL_FACE);
					else {
						glEnable(GL_CULL_FACE);
						glCullFace(current_pipeline->rasterization.cull_mode == pipeline::CullMode::Front
							? GL_FRONT : GL_BACK);
					}
				}
			}

			void operator()(execution::BindResourceGroupCommand const& command) {
				if (!current_pipeline) throw std::logic_error("OpenGL pipeline must be bound first");
				if (command.index != 0u) throw std::invalid_argument("OpenGL only supports resource space zero");
				auto const& group = bindings->resource_groups[command.group.value].get();
				for (auto const& resource_binding : group.bindings) {
					auto unit = resource_binding.slot + resource_binding.array_element;
					auto const& metadata = FindBinding(group, resource_binding.slot);
					if (auto buffer = std::get_if<pipeline::NativePipelineBufferBinding<Backend>>(&resource_binding.value)) {
						auto const& resource = buffer->impl.get();
						auto size = buffer->size == pipeline::PipelineWholeBuffer
							? resource.size - buffer->offset : buffer->size;
						auto target = metadata.flags.Test(ResourceFlagBits::UniformBuffer)
							? GL_UNIFORM_BUFFER : GL_SHADER_STORAGE_BUFFER;
						glBindBufferRange(target, unit, resource.impl, buffer->offset, size);
					}
					else if (auto view = std::get_if<pipeline::NativePipelineViewBinding<Backend>>(&resource_binding.value)) {
						if (metadata.flags.Test(ResourceFlagBits::StorageBinding)) {
							auto const& texture = std::get<Backend::GLTextureView>(view->impl.get().impl);
							glBindImageTexture(unit, texture.impl, 0, GL_TRUE, 0, GL_READ_WRITE, texture.format);
						}
						else BindTexture(unit, view->impl.get());
					}
					else if (auto sampler = std::get_if<pipeline::NativePipelineSamplerBinding<Backend>>(&resource_binding.value)) {
						glBindSampler(unit, sampler->impl.get().impl);
					}
					else if (auto combined = std::get_if<pipeline::NativePipelineCombinedBinding<Backend>>(&resource_binding.value)) {
						BindTexture(unit, combined->view.get());
						glBindSampler(unit, combined->sampler.get().impl);
					}
				}
			}

			void operator()(execution::BindVertexBufferCommand const& command) {
				auto const& resource = bindings->resources[command.resource.value].get();
				glBindVertexArray(vertex_array);
				glBindVertexBuffer(command.slot, resource.impl, command.offset, command.stride);
			}

			void operator()(execution::BindIndexBufferCommand const& command) {
				index_buffer = bindings->resources[command.resource.value].get().impl;
				index_offset = command.offset;
				index_uint32 = command.uint32;
				glBindVertexArray(vertex_array);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
			}

			void operator()(execution::SetViewportCommand const& command) {
				glViewport(
					static_cast<GLint>(command.x), static_cast<GLint>(command.y),
					static_cast<GLsizei>(command.width), static_cast<GLsizei>(command.height)
				);
				glDepthRangef(command.minimum_depth, command.maximum_depth);
			}

			void operator()(execution::SetScissorCommand const& command) {
				glEnable(GL_SCISSOR_TEST);
				glScissor(command.x, command.y, command.width, command.height);
			}

			void operator()(execution::DrawCommand const& command) {
				if (!current_pipeline || current_pipeline->compute || !rendering) throw std::logic_error("Invalid OpenGL draw state");
				if (command.first_instance != 0u) throw std::invalid_argument("OpenGL base instance is unavailable on the portable path");
				glDrawArraysInstanced(
					OpenGLPrimitive(current_pipeline->primitive.topology), command.first_vertex,
					command.vertex_count, command.instance_count
				);
			}

			void operator()(execution::DrawIndexedCommand const& command) {
				if (!current_pipeline || current_pipeline->compute || !rendering || !index_buffer) {
					throw std::logic_error("Invalid OpenGL indexed draw state");
				}
				if (command.first_instance != 0u || command.vertex_offset != 0) {
					throw std::invalid_argument("OpenGL base vertex/instance is unavailable on the portable path");
				}
				auto index_size = index_uint32 ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
				glDrawElementsInstanced(
					OpenGLPrimitive(current_pipeline->primitive.topology), command.index_count,
					index_uint32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
					reinterpret_cast<void const*>(index_offset + command.first_index * index_size),
					command.instance_count
				);
			}

			void operator()(execution::DispatchCommand const& command) {
				if (!current_pipeline || !current_pipeline->compute || rendering) throw std::logic_error("Invalid OpenGL dispatch state");
				glDispatchCompute(command.group_count_x, command.group_count_y, command.group_count_z);
			}

			void operator()(execution::CopyBufferCommand const& command) {
				auto const& source = bindings->resources[command.source.value].get();
				auto const& destination = bindings->resources[command.destination.value].get();
				if (command.source_offset + command.size > source.size ||
					command.destination_offset + command.size > destination.size) {
					throw std::out_of_range("OpenGL buffer copy exceeds the resource range");
				}
				glBindBuffer(GL_COPY_READ_BUFFER, source.impl);
				glBindBuffer(GL_COPY_WRITE_BUFFER, destination.impl);
				glCopyBufferSubData(
					GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, command.source_offset,
					command.destination_offset, command.size
				);
			}

			void operator()(execution::PresentCommand const& command) {
				auto const& source = bindings->resources[command.source.value].get();
				if (source.type != Backend::GLResource::Type::Texture) {
					throw std::invalid_argument("OpenGL presentation source must be a texture");
				}
				auto const& target = bindings->presentation_targets[command.target.value];
				auto CreateEntry = [&target, this]() {
					Backend::PresentationEntry result;
					result.target = target;
#if defined(_WIN32)
					result.dc = GetDC(target);
					if (!result.dc) {
						throw std::runtime_error("OpenGL could not acquire the presentation DC");
					}
					if (GetPixelFormat(result.dc) == 0) {
						auto source_dc = logical_device->instance->dc;
						auto pixel_format = GetPixelFormat(source_dc);
						PIXELFORMATDESCRIPTOR descriptor{};
						if (!DescribePixelFormat(
							source_dc, pixel_format, sizeof(descriptor), &descriptor
						) || !SetPixelFormat(result.dc, pixel_format, &descriptor)) {
							throw std::runtime_error("OpenGL could not configure the presentation DC");
						}
					}
#elif defined(__linux__)
					auto PopulateEntry = [&result, this](auto const& native_target) {
						using Target = std::remove_cvref_t<decltype(native_target)>;
						if constexpr (std::same_as<Target, Backend::X11PresentationTarget>) {
							result.drawable = static_cast<GLXDrawable>(native_target.window);
						}
						else {
							auto const& egl = std::get<Backend::Instance::EGL>(
								logical_device->instance->gl_handle
							);
							result.drawable = egl.draw;
						}
					};
					std::visit(PopulateEntry, target);
#elif defined(__ANDROID__)
					auto const& egl = std::get<Backend::Instance::EGL>(logical_device->instance->gl_handle);
					result.drawable = egl.draw;
#endif
					return result;
				};
				auto presentation = logical_device->presentation_cache->Acquire(target, CreateEntry);
#if defined(_WIN32)
				auto previous_dc = wglGetCurrentDC();
				auto context = wglGetCurrentContext();
				if (!presentation.Get().dc || !wglMakeCurrent(presentation.Get().dc, context)) {
					throw std::runtime_error("OpenGL could not bind the presentation target");
				}
#endif
				GLuint read_framebuffer = 0u;
				glGenFramebuffers(1u, &read_framebuffer);
				glBindFramebuffer(GL_READ_FRAMEBUFFER, read_framebuffer);
				if (source.target == GL_TEXTURE_2D || source.target == GL_TEXTURE_CUBE_MAP) {
					glFramebufferTexture2D(
						GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, source.target, source.impl, 0
					);
				}
				else {
					glFramebufferTextureLayer(
						GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, source.impl, 0, 0
					);
				}
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0u);
				glBlitFramebuffer(
					0, 0, source.width, source.height, 0, 0, source.width, source.height,
					GL_COLOR_BUFFER_BIT, GL_NEAREST
				);
#if defined(_WIN32)
				if (!SwapBuffers(presentation.Get().dc)) {
					glDeleteFramebuffers(1u, &read_framebuffer);
					wglMakeCurrent(previous_dc, context);
					throw std::runtime_error("OpenGL SwapBuffers failed");
				}
				glDeleteFramebuffers(1u, &read_framebuffer);
				wglMakeCurrent(previous_dc, context);
#elif defined(__linux__)
				auto PresentTarget = [&presentation, this](auto const& native_target) {
					using Target = std::remove_cvref_t<decltype(native_target)>;
					if constexpr (std::same_as<Target, Backend::X11PresentationTarget>) {
						glXSwapBuffers(native_target.display, native_target.window);
					}
					else {
						(void)presentation;
						auto const& egl = std::get<Backend::Instance::EGL>(
							logical_device->instance->gl_handle
						);
						if (eglSwapBuffers(egl.display, egl.draw) != EGL_TRUE) {
							throw std::runtime_error("OpenGL ES eglSwapBuffers failed");
						}
					}
				};
				std::visit(PresentTarget, target);
#elif defined(__ANDROID__)
				auto const& egl = std::get<Backend::Instance::EGL>(logical_device->instance->gl_handle);
				if (eglSwapBuffers(egl.display, egl.draw) != EGL_TRUE) {
					throw std::runtime_error("OpenGL ES eglSwapBuffers failed");
				}
#endif
#if !defined(_WIN32)
				glDeleteFramebuffers(1u, &read_framebuffer);
#endif
			}
		};

		void ExecuteOpenGLGraph(void* operation_state) {
			auto& execution = *static_cast<Backend::GraphExecution*>(operation_state);
			auto const& bindings = execution.graph->impl->bindings;
			if (execution.batches.empty()) {
				return;
			}
			GLuint vertex_array = 0u;
			glGenVertexArrays(1u, &vertex_array);
			OpenGLCommandRecorder recorder{ &bindings, &execution.batches.front().queue->impl };
			recorder.vertex_array = vertex_array;
			for (auto& batch : execution.batches) {
				auto const& commands = batch.commands.Get();
				std::size_t barrier_index = 0u;
				for (std::size_t index = 0u; index < commands.size(); ++index) {
					while (barrier_index < batch.barriers.size() &&
						batch.barriers[barrier_index].command_offset == index) {
						if (batch.barriers[barrier_index].bits) {
							glMemoryBarrier(batch.barriers[barrier_index].bits);
						}
						++barrier_index;
					}
					std::visit(recorder, commands[index]);
				}
			}
			if (recorder.rendering) throw std::logic_error("OpenGL rendering scope was not ended");
			glFinish();
			glDeleteVertexArrays(1u, &vertex_array);
		}
	}

	Backend::ExecutableGraph CompileCommandGraph(Backend::CommandGraph const& graph) {
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
			auto commands = queue->command_pool->Acquire(CreateCommandList{}, ResetCommandList{});
			std::vector<Backend::GraphExecution::Batch::BarrierPoint> barriers;
			for (auto node_id : batch_plan.nodes) {
				auto const& node = native_graph.descriptor.nodes[node_id.value];
				GLbitfield bits = 0u;
				for (auto const& access : node.accesses) {
					bits |= OpenGLBarrierBits(access.flags);
				}
				if (bits) {
					barriers.push_back({ commands.Get().size(), bits });
				}
				commands.Get().insert(commands.Get().end(), node.commands.begin(), node.commands.end());
			}
			result.batches.emplace_back(queue, std::move(commands), std::move(barriers));
		}
		return result;
	}

	void StartGraphExecution(
		Backend::GraphExecution& graph_execution,
		execution::GraphCompletion const& completion
	) {
		auto queue = graph_execution.batches.empty()
			? graph_execution.scheduler->queues.graphics
			: graph_execution.batches.front().queue;
		if (!queue) queue = graph_execution.scheduler->queues.compute;
		if (!queue) queue = graph_execution.scheduler->queues.copy;
		queue->Enqueue({
			.operation_state = &graph_execution,
			.Execute = ExecuteOpenGLGraph,
			.completion = completion,
			.keep_alive = queue
		});
	}


	std::shared_ptr<Backend::GLScheduler::QueueState> const&
	Backend::GLScheduler::QueueCollection::Select(
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
		throw std::invalid_argument("Command graph batch requires an unavailable OpenGL queue");
	}

	Backend::GLScheduler::QueueState::QueueState(
		LogicalDevice const& logical_device_,
		std::shared_ptr<CommandPool> const& command_pool_
	) : impl(logical_device_), command_pool(command_pool_),
		worker(&QueueState::Run, this) {
		std::unique_lock<std::mutex> lock(mutex);
		auto IsReady = [this]() noexcept { return ready; };
		condition.wait(lock, IsReady);
		if (startup_error) {
			std::rethrow_exception(startup_error);
		}
	}

	void Backend::GLScheduler::QueueState::Enqueue(Submission const& submission) {
		{
			std::unique_lock<std::mutex> lock(mutex);
			submissions.emplace_back(submission);
		}
		condition.notify_one();
	}

	void Backend::GLScheduler::QueueState::Run(std::stop_token stop_token, QueueState* self) noexcept {
		try {
			Backend::ShareContextOnThisThread(*self->impl.instance);
		}
		catch (...) {
			{
				std::unique_lock<std::mutex> lock(self->mutex);
				self->startup_error = std::current_exception();
				self->ready = true;
			}
			self->condition.notify_all();
			return;
		}
		{
			std::unique_lock<std::mutex> lock(self->mutex);
			self->ready = true;
		}
		self->condition.notify_all();
		auto NotifyStopped = [self]() noexcept {
			self->condition.notify_all();
		};
		std::stop_callback stop_callback(stop_token, NotifyStopped);
		while (true) {
			Submission submission;
			{
				std::unique_lock<std::mutex> lock(self->mutex);
				auto HasSubmissionOrStopped = [self, stop_token]() noexcept {
					return !self->submissions.empty() || stop_token.stop_requested();
				};
				self->condition.wait(lock, HasSubmissionOrStopped);
				if (self->submissions.empty()) {
					break;
				}
				submission = self->submissions.front();
				self->submissions.pop_front();
			}
			try {
				submission.Execute(submission.operation_state);
				auto CompleteValue = [
					completion = submission.completion,
					keep_alive = submission.keep_alive
				]() noexcept {
					(void)keep_alive;
					completion.SetValue(completion.operation);
				};
				self->impl.completion_service->Enqueue(PollCompleted, CompleteValue);
			}
			catch (...) {
				auto error = std::current_exception();
				auto CompleteError = [
					completion = submission.completion,
					keep_alive = submission.keep_alive,
					error
				]() noexcept {
					(void)keep_alive;
					completion.SetError(completion.operation, error);
				};
				self->impl.completion_service->Enqueue(PollCompleted, CompleteError);
			}
		}
	}

}
#endif // !defined(__APPLE__)
