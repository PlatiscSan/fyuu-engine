module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <array>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <format>
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
	struct OpenGLTransferFormat {
		GLenum format;
		GLenum type;
		GLenum attachment = GL_COLOR_ATTACHMENT0;
		std::uint32_t block_width = 1u;
		std::uint32_t block_height = 1u;
		std::uint32_t block_bytes;
		bool compressed = false;
	};

	OpenGLTransferFormat GetOpenGLTransferFormat(GLenum format) {
		switch (format) {
		case GL_R8: return { GL_RED, GL_UNSIGNED_BYTE, GL_COLOR_ATTACHMENT0, 1u, 1u, 1u };
		case GL_R8_SNORM: return { GL_RED, GL_BYTE, GL_COLOR_ATTACHMENT0, 1u, 1u, 1u };
		case GL_R8UI: return { GL_RED_INTEGER, GL_UNSIGNED_BYTE, GL_COLOR_ATTACHMENT0, 1u, 1u, 1u };
		case GL_R8I: return { GL_RED_INTEGER, GL_BYTE, GL_COLOR_ATTACHMENT0, 1u, 1u, 1u };
		case GL_RG8: return { GL_RG, GL_UNSIGNED_BYTE, GL_COLOR_ATTACHMENT0, 1u, 1u, 2u };
		case GL_RG8_SNORM: return { GL_RG, GL_BYTE, GL_COLOR_ATTACHMENT0, 1u, 1u, 2u };
		case GL_RG8UI: return { GL_RG_INTEGER, GL_UNSIGNED_BYTE, GL_COLOR_ATTACHMENT0, 1u, 1u, 2u };
		case GL_RG8I: return { GL_RG_INTEGER, GL_BYTE, GL_COLOR_ATTACHMENT0, 1u, 1u, 2u };
		case GL_RGBA8: case GL_SRGB8_ALPHA8:
			return { GL_RGBA, GL_UNSIGNED_BYTE, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_RGBA8_SNORM: return { GL_RGBA, GL_BYTE, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_RGBA8UI: return { GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_RGBA8I: return { GL_RGBA_INTEGER, GL_BYTE, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_R16: return { GL_RED, GL_UNSIGNED_SHORT, GL_COLOR_ATTACHMENT0, 1u, 1u, 2u };
		case GL_R16_SNORM: return { GL_RED, GL_SHORT, GL_COLOR_ATTACHMENT0, 1u, 1u, 2u };
		case GL_R16UI: return { GL_RED_INTEGER, GL_UNSIGNED_SHORT, GL_COLOR_ATTACHMENT0, 1u, 1u, 2u };
		case GL_R16I: return { GL_RED_INTEGER, GL_SHORT, GL_COLOR_ATTACHMENT0, 1u, 1u, 2u };
		case GL_R16F: return { GL_RED, GL_HALF_FLOAT, GL_COLOR_ATTACHMENT0, 1u, 1u, 2u };
		case GL_RG16: return { GL_RG, GL_UNSIGNED_SHORT, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_RG16_SNORM: return { GL_RG, GL_SHORT, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_RG16UI: return { GL_RG_INTEGER, GL_UNSIGNED_SHORT, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_RG16I: return { GL_RG_INTEGER, GL_SHORT, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_RG16F: return { GL_RG, GL_HALF_FLOAT, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_RGBA16: return { GL_RGBA, GL_UNSIGNED_SHORT, GL_COLOR_ATTACHMENT0, 1u, 1u, 8u };
		case GL_RGBA16_SNORM: return { GL_RGBA, GL_SHORT, GL_COLOR_ATTACHMENT0, 1u, 1u, 8u };
		case GL_RGBA16UI: return { GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, GL_COLOR_ATTACHMENT0, 1u, 1u, 8u };
		case GL_RGBA16I: return { GL_RGBA_INTEGER, GL_SHORT, GL_COLOR_ATTACHMENT0, 1u, 1u, 8u };
		case GL_RGBA16F: return { GL_RGBA, GL_HALF_FLOAT, GL_COLOR_ATTACHMENT0, 1u, 1u, 8u };
		case GL_R32UI: return { GL_RED_INTEGER, GL_UNSIGNED_INT, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_R32I: return { GL_RED_INTEGER, GL_INT, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_R32F: return { GL_RED, GL_FLOAT, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_RG32UI: return { GL_RG_INTEGER, GL_UNSIGNED_INT, GL_COLOR_ATTACHMENT0, 1u, 1u, 8u };
		case GL_RG32I: return { GL_RG_INTEGER, GL_INT, GL_COLOR_ATTACHMENT0, 1u, 1u, 8u };
		case GL_RG32F: return { GL_RG, GL_FLOAT, GL_COLOR_ATTACHMENT0, 1u, 1u, 8u };
		case GL_RGBA32UI: return { GL_RGBA_INTEGER, GL_UNSIGNED_INT, GL_COLOR_ATTACHMENT0, 1u, 1u, 16u };
		case GL_RGBA32I: return { GL_RGBA_INTEGER, GL_INT, GL_COLOR_ATTACHMENT0, 1u, 1u, 16u };
		case GL_RGBA32F: return { GL_RGBA, GL_FLOAT, GL_COLOR_ATTACHMENT0, 1u, 1u, 16u };
		case GL_RGB10_A2: return { GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_RGB10_A2UI: return { GL_RGBA_INTEGER, GL_UNSIGNED_INT_2_10_10_10_REV, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_R11F_G11F_B10F: return { GL_RGB, GL_UNSIGNED_INT_10F_11F_11F_REV, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_RGB9_E5: return { GL_RGB, GL_UNSIGNED_INT_5_9_9_9_REV, GL_COLOR_ATTACHMENT0, 1u, 1u, 4u };
		case GL_DEPTH_COMPONENT16: return { GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, GL_DEPTH_ATTACHMENT, 1u, 1u, 2u };
		case GL_DEPTH24_STENCIL8: return { GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, GL_DEPTH_STENCIL_ATTACHMENT, 1u, 1u, 4u };
		case GL_DEPTH_COMPONENT32F: return { GL_DEPTH_COMPONENT, GL_FLOAT, GL_DEPTH_ATTACHMENT, 1u, 1u, 4u };
		case GL_DEPTH32F_STENCIL8: return { GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, GL_DEPTH_STENCIL_ATTACHMENT, 1u, 1u, 8u };
		case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT: case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
		case GL_COMPRESSED_RED_RGTC1: case GL_COMPRESSED_SIGNED_RED_RGTC1:
			return { GL_NONE, GL_NONE, GL_COLOR_ATTACHMENT0, 4u, 4u, 8u, true };
		case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT: case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
		case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT: case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
		case GL_COMPRESSED_RG_RGTC2: case GL_COMPRESSED_SIGNED_RG_RGTC2:
		case GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT: case GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT:
		case GL_COMPRESSED_RGBA_BPTC_UNORM: case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM:
			return { GL_NONE, GL_NONE, GL_COLOR_ATTACHMENT0, 4u, 4u, 16u, true };
		default: throw std::invalid_argument("OpenGL texture format has no transfer mapping");
		}
	}

	std::size_t OpenGLTransferSize(
		fyuu_rhi::TextureDataLayout const& layout,
		fyuu_rhi::TextureRegion const& region,
		OpenGLTransferFormat const& format
	) noexcept {
		auto block_rows = (region.height + format.block_height - 1u) / format.block_height;
		auto rows_per_image = (layout.rows_per_image + format.block_height - 1u) /
			format.block_height;
		auto images = region.depth * region.array_layer_count;
		return images == 0u ? 0u : layout.bytes_per_row *
			(rows_per_image * (images - 1u) + block_rows);
	}

	void ValidateOpenGLTransferLayout(
		fyuu_rhi::TextureDataLayout const& layout,
		fyuu_rhi::TextureRegion const& region,
		OpenGLTransferFormat const& format,
		std::size_t buffer_size,
		char const* range_message
	) {
		auto block_columns = (region.width + format.block_width - 1u) / format.block_width;
		auto minimum_row_size = static_cast<std::size_t>(block_columns) * format.block_bytes;
		if (layout.bytes_per_row < minimum_row_size ||
			layout.bytes_per_row % format.block_bytes != 0u) {
			throw std::invalid_argument("OpenGL texture transfer row pitch is invalid");
		}
		if (layout.rows_per_image < region.height) {
			throw std::invalid_argument("OpenGL texture transfer image pitch is invalid");
		}
		auto transfer_size = OpenGLTransferSize(layout, region, format);
		if (layout.offset > buffer_size || transfer_size > buffer_size - layout.offset) {
			throw std::out_of_range(range_message);
		}
	}

	bool HasOpenGLCompressedPixelStorage() noexcept {
		return GLAD_GL_VERSION_4_2 || GLAD_GL_ARB_compressed_texture_pixel_storage;
	}

	struct OpenGLPixelUnpackScope {
		GLint buffer;
		GLint alignment;
		GLint row_length;
		GLint image_height;
		GLint block_width = 0;
		GLint block_height = 0;
		GLint block_depth = 0;
		GLint block_size = 0;
		bool compressed_pixel_storage;

		OpenGLPixelUnpackScope() noexcept {
			glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &buffer);
			glGetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
			glGetIntegerv(GL_UNPACK_ROW_LENGTH, &row_length);
			glGetIntegerv(GL_UNPACK_IMAGE_HEIGHT, &image_height);
			compressed_pixel_storage = HasOpenGLCompressedPixelStorage();
			if (compressed_pixel_storage) {
				glGetIntegerv(GL_UNPACK_COMPRESSED_BLOCK_WIDTH, &block_width);
				glGetIntegerv(GL_UNPACK_COMPRESSED_BLOCK_HEIGHT, &block_height);
				glGetIntegerv(GL_UNPACK_COMPRESSED_BLOCK_DEPTH, &block_depth);
				glGetIntegerv(GL_UNPACK_COMPRESSED_BLOCK_SIZE, &block_size);
			}
		}

		~OpenGLPixelUnpackScope() noexcept {
			if (compressed_pixel_storage) {
				glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_SIZE, block_size);
				glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_DEPTH, block_depth);
				glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_HEIGHT, block_height);
				glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_WIDTH, block_width);
			}
			glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, image_height);
			glPixelStorei(GL_UNPACK_ROW_LENGTH, row_length);
			glPixelStorei(GL_UNPACK_ALIGNMENT, alignment);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, buffer);
		}
	};

	struct OpenGLPixelPackScope {
		GLint buffer;
		GLint alignment;
		GLint row_length;
		GLint image_height;
		GLint framebuffer;
		GLint block_width = 0;
		GLint block_height = 0;
		GLint block_depth = 0;
		GLint block_size = 0;
		bool compressed_pixel_storage;

		OpenGLPixelPackScope() noexcept {
			glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &buffer);
			glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
			glGetIntegerv(GL_PACK_ROW_LENGTH, &row_length);
			glGetIntegerv(GL_PACK_IMAGE_HEIGHT, &image_height);
			glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &framebuffer);
			compressed_pixel_storage = HasOpenGLCompressedPixelStorage();
			if (compressed_pixel_storage) {
				glGetIntegerv(GL_PACK_COMPRESSED_BLOCK_WIDTH, &block_width);
				glGetIntegerv(GL_PACK_COMPRESSED_BLOCK_HEIGHT, &block_height);
				glGetIntegerv(GL_PACK_COMPRESSED_BLOCK_DEPTH, &block_depth);
				glGetIntegerv(GL_PACK_COMPRESSED_BLOCK_SIZE, &block_size);
			}
		}

		~OpenGLPixelPackScope() noexcept {
			glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
			if (compressed_pixel_storage) {
				glPixelStorei(GL_PACK_COMPRESSED_BLOCK_SIZE, block_size);
				glPixelStorei(GL_PACK_COMPRESSED_BLOCK_DEPTH, block_depth);
				glPixelStorei(GL_PACK_COMPRESSED_BLOCK_HEIGHT, block_height);
				glPixelStorei(GL_PACK_COMPRESSED_BLOCK_WIDTH, block_width);
			}
			glPixelStorei(GL_PACK_IMAGE_HEIGHT, image_height);
			glPixelStorei(GL_PACK_ROW_LENGTH, row_length);
			glPixelStorei(GL_PACK_ALIGNMENT, alignment);
			glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
		}
	};

	bool PollCompleted() noexcept {
		return true;
	}

	GLsync ExecuteSchedule(void*) {
		auto sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0u);
		if (!sync) {
			throw std::runtime_error("OpenGL could not create a scheduler completion fence");
		}
		glFlush();
		return sync;
	}

	GLsync ExecuteDeferredDestroy(void* operation_state) noexcept {
		auto const& deferred_destroy = *static_cast<fyuu_rhi::execution::DeferredDestroy const*>(
			operation_state
		);
		deferred_destroy.Destroy(deferred_destroy.object);
		return nullptr;
	}

	struct OpenGLMapOperation {
		fyuu_rhi::opengl::Backend::Resource* resource = nullptr;
		fyuu_rhi::execution::ResourceMapRequest const* request = nullptr;
	};

	GLsync ExecuteMapResource(void* operation_state) {
		auto const& operation = *static_cast<OpenGLMapOperation const*>(operation_state);
		auto const& request = *operation.request;
		GLbitfield access = 0u;
		if (request.read) access |= GL_MAP_READ_BIT;
		if (request.write) access |= GL_MAP_WRITE_BIT;
		void* mapped = nullptr;
		if (GLAD_GL_ARB_direct_state_access) {
			mapped = glMapNamedBufferRange(
				operation.resource->impl,
				request.offset,
				request.size,
				access
			);
		}
		else {
			glBindBuffer(GL_COPY_READ_BUFFER, operation.resource->impl);
			mapped = glMapBufferRange(
				GL_COPY_READ_BUFFER,
				request.offset,
				request.size,
				access
			);
			glBindBuffer(GL_COPY_READ_BUFFER, 0u);
		}
		if (!mapped) {
			auto error = glGetError();
			throw std::runtime_error(std::format(
				"OpenGL buffer mapping failed: object={}, visible={}, offset={}, size={}, access=0x{:x}, error=0x{:x}",
				operation.resource->impl,
				glIsBuffer(operation.resource->impl) == GL_TRUE,
				request.offset,
				request.size,
				access,
				error
			));
		}
		request.completion.SetValue(
			request.completion.operation,
			static_cast<std::byte*>(mapped)
		);
		return nullptr;
	}

	struct OpenGLUnmapOperation {
		fyuu_rhi::opengl::Backend::Resource* resource = nullptr;
	};

	GLsync ExecuteUnmapResource(void* operation_state) {
		auto const& operation = *static_cast<OpenGLUnmapOperation const*>(operation_state);
		GLboolean result;
		if (GLAD_GL_ARB_direct_state_access) {
			result = glUnmapNamedBuffer(operation.resource->impl);
		}
		else {
			glBindBuffer(GL_COPY_READ_BUFFER, operation.resource->impl);
			result = glUnmapBuffer(GL_COPY_READ_BUFFER);
			glBindBuffer(GL_COPY_READ_BUFFER, 0u);
		}
		if (result != GL_TRUE) {
			throw std::runtime_error("OpenGL mapped buffer contents became invalid");
		}
		return nullptr;
	}

	void CompleteMapError(void* operation, std::exception_ptr const& error) noexcept {
		auto const& map = *static_cast<OpenGLMapOperation const*>(operation);
		map.request->completion.SetError(map.request->completion.operation, error);
	}

	void WaitForPresentationFrame(GLsync& sync) {
		if (!sync) {
			return;
		}
		while (true) {
			auto result = glClientWaitSync(sync, GL_SYNC_FLUSH_COMMANDS_BIT, 1'000'000u);
			if (result == GL_ALREADY_SIGNALED || result == GL_CONDITION_SATISFIED) {
				break;
			}
			if (result == GL_WAIT_FAILED) {
				throw std::runtime_error("OpenGL presentation frame wait failed");
			}
		}
		glDeleteSync(sync);
		sync = nullptr;
	}

	GLsync& AcquirePresentationFrame(
		fyuu_rhi::opengl::Backend::PresentationEntry& entry,
		std::uint32_t frames_in_flight
	) {
		if (!entry.frames || entry.frames->slots.size() != frames_in_flight) {
			if (entry.frames) {
				for (auto& sync : entry.frames->slots) {
					WaitForPresentationFrame(sync);
				}
			}
			entry.frames = std::make_shared<
				fyuu_rhi::opengl::Backend::PresentationEntry::FrameState
			>();
			entry.frames->slots.resize(frames_in_flight, nullptr);
		}
		auto& sync = entry.frames->slots[entry.frames->next_slot];
		entry.frames->next_slot = (entry.frames->next_slot + 1u) % entry.frames->slots.size();
		WaitForPresentationFrame(sync);
		return sync;
	}

	std::vector<fyuu_rhi::execution::GraphCommand> CreateCommandList() {
		return {};
	}

	void ResetCommandList(std::vector<fyuu_rhi::execution::GraphCommand>& commands) {
		commands.clear();
	}

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

namespace {
	using namespace fyuu_rhi::opengl;
	using fyuu_rhi::ResourceFlagBits;
	namespace execution = fyuu_rhi::execution;
	namespace pipeline = fyuu_rhi::pipeline;

		using Bindings = execution::NativeCommandGraphBindings<Backend>;

		struct OpenGLResourceSnapshot {
			GLuint impl = 0u;
			GLenum target = 0u;
			GLenum format = 0u;
			std::size_t size = 0u;
			std::uint32_t width = 0u;
			std::uint32_t height = 0u;
			Backend::GLResource::Type type = Backend::GLResource::Type::Buffer;
		};

		struct OpenGLBindingsSnapshot {
			std::vector<OpenGLResourceSnapshot> resources;
			std::vector<std::vector<OpenGLResourceSnapshot>> group_buffers;
			std::vector<Backend::PresentationTarget> presentation_targets;
		};

		OpenGLResourceSnapshot CaptureResource(Backend::Resource const& resource) noexcept {
			return {
				resource.impl,
				resource.target,
				resource.format,
				resource.size,
				resource.width,
				resource.height,
				resource.type
			};
		}

		std::shared_ptr<OpenGLBindingsSnapshot const> CaptureBindings(
			Bindings const& bindings
		) {
			auto result = std::make_shared<OpenGLBindingsSnapshot>();
			result->resources.reserve(bindings.resources.size());
			for (auto const& resource : bindings.resources) {
				result->resources.emplace_back(CaptureResource(resource.get()));
			}
			result->group_buffers.reserve(bindings.resource_groups.size());
			for (auto const& group_reference : bindings.resource_groups) {
				auto const& group = group_reference.get();
				auto& buffers = result->group_buffers.emplace_back(group.bindings.size());
				for (std::size_t index = 0u; index < group.bindings.size(); ++index) {
					if (auto binding = std::get_if<pipeline::NativePipelineBufferBinding<Backend>>(
						&group.bindings[index].value
					)) {
						buffers[index] = CaptureResource(binding->impl.get());
					}
				}
			}
			result->presentation_targets = bindings.presentation_targets;
			return result;
		}

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
			auto texture = std::get_if<Backend::GLTextureView>(&view);
			if (!texture) {
				auto buffer = std::get_if<Backend::GLBufferView>(&view);
				if (!buffer) throw std::invalid_argument("OpenGL texture binding requires a view");
				glActiveTexture(GL_TEXTURE0 + unit);
				glBindTexture(GL_TEXTURE_BUFFER, buffer->impl);
				return;
			}
			glActiveTexture(GL_TEXTURE0 + unit);
			glBindTexture(texture->target, texture->impl);
		}

		void AttachView(GLenum attachment, Backend::View const& view) {
			auto texture = std::get_if<Backend::GLTextureView>(&view);
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
			OpenGLBindingsSnapshot const* snapshot;
			Backend::LogicalDevice const* logical_device;
			Backend::Pipeline const* current_pipeline = nullptr;
			GLuint framebuffer = 0u;
			GLuint vertex_array = 0u;
			GLuint index_buffer = 0u;
			std::size_t index_offset = 0u;
			bool index_uint32 = false;
			std::optional<execution::BeginRenderingCommand> rendering;

			static void SetCoordinateConvention(execution::CoordinateConvention convention) {
				if (!GLAD_GL_VERSION_4_5 && !GLAD_GL_ARB_clip_control) {
					if (convention == execution::CoordinateConvention::Engine) {
						throw std::runtime_error(
							"OpenGL engine coordinates require GL 4.5 or GL_ARB_clip_control"
						);
					}
					return;
				}
				if (convention == execution::CoordinateConvention::Engine) {
					glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE);
				}
				else {
					glClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
				}
			}

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
				}
				if (!draw_buffers.empty()) {
					glDrawBuffers(static_cast<GLsizei>(draw_buffers.size()), draw_buffers.data());
				}
				if (command.depth_stencil) {
					auto const& attachment = *command.depth_stencil;
					auto const& view = bindings->views[attachment.view.value].get();
					auto const& texture = std::get<Backend::GLTextureView>(view);
					AttachView(OpenGLAttachment(texture.format), view);
				}
				auto framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
				if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE) {
					auto error = glGetError();
					throw std::runtime_error(std::format(
						"OpenGL rendering framebuffer is incomplete: status 0x{:x}, error 0x{:x}",
						framebuffer_status,
						error
					));
				}
				for (std::size_t index = 0u; index < command.colors.size(); ++index) {
					if (!command.colors[index].load) {
						std::array clear{
							command.colors[index].clear_red, command.colors[index].clear_green,
							command.colors[index].clear_blue, command.colors[index].clear_alpha
						};
						glClearBufferfv(GL_COLOR, static_cast<GLint>(index), clear.data());
					}
				}
				if (command.depth_stencil) {
					auto const& attachment = *command.depth_stencil;
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
				SetCoordinateConvention(command.coordinate_convention);
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
				for (std::size_t binding_index = 0u; binding_index < group.bindings.size(); ++binding_index) {
					auto const& resource_binding = group.bindings[binding_index];
					auto unit = resource_binding.slot + resource_binding.array_element;
					auto const& metadata = FindBinding(group, resource_binding.slot);
					if (auto buffer = std::get_if<pipeline::NativePipelineBufferBinding<Backend>>(&resource_binding.value)) {
						auto const& resource = snapshot->group_buffers[command.group.value][binding_index];
						auto size = buffer->size == pipeline::PipelineWholeBuffer
							? resource.size - buffer->offset : buffer->size;
						auto target = metadata.flags.Test(ResourceFlagBits::UniformBuffer)
							? GL_UNIFORM_BUFFER : GL_SHADER_STORAGE_BUFFER;
						glBindBufferRange(target, unit, resource.impl, buffer->offset, size);
					}
					else if (auto view = std::get_if<pipeline::NativePipelineViewBinding<Backend>>(&resource_binding.value)) {
						if (metadata.flags.Test(ResourceFlagBits::StorageBinding)) {
							auto const& texture = std::get<Backend::GLTextureView>(view->get());
							glBindImageTexture(unit, texture.impl, 0, GL_TRUE, 0, GL_READ_WRITE, texture.format);
						}
						else BindTexture(unit, view->get());
					}
					else if (auto sampler = std::get_if<pipeline::NativePipelineSamplerBinding<Backend>>(&resource_binding.value)) {
						glBindSampler(unit, sampler->get().impl);
					}
					else if (auto combined = std::get_if<pipeline::NativePipelineCombinedBinding<Backend>>(&resource_binding.value)) {
						BindTexture(unit, combined->view.get());
						glBindSampler(unit, combined->sampler.get().impl);
					}
				}
			}

			void operator()(execution::BindVertexBufferCommand const& command) {
				auto const& resource = snapshot->resources[command.resource.value];
				glBindVertexArray(vertex_array);
				glBindVertexBuffer(command.slot, resource.impl, command.offset, command.stride);
			}

			void operator()(execution::BindIndexBufferCommand const& command) {
				index_buffer = snapshot->resources[command.resource.value].impl;
				index_offset = command.offset;
				index_uint32 = command.uint32;
				glBindVertexArray(vertex_array);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer);
			}

			void operator()(execution::SetViewportCommand const& command) {
				SetCoordinateConvention(command.coordinate_convention);
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
				auto const& source = snapshot->resources[command.source.value];
				auto const& destination = snapshot->resources[command.destination.value];
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

			void operator()(execution::CopyBufferToTextureCommand const& command) {
				auto const& source = snapshot->resources[command.source.value];
				auto const& destination = snapshot->resources[command.destination.value];
				auto const& region = command.destination_region;
				auto format = GetOpenGLTransferFormat(destination.format);
				ValidateOpenGLTransferLayout(command.source_layout, region, format, source.size,
					"OpenGL buffer-to-texture copy exceeds the source buffer range");
				auto block_columns = (region.width + format.block_width - 1u) /
					format.block_width;
				if (format.compressed && !HasOpenGLCompressedPixelStorage() &&
					(command.source_layout.bytes_per_row != block_columns * format.block_bytes ||
						command.source_layout.rows_per_image != region.height)) {
					throw std::invalid_argument(
						"OpenGL compressed upload row pitch requires compressed pixel storage"
					);
				}
				OpenGLPixelUnpackScope pixel_store;
				glBindBuffer(GL_PIXEL_UNPACK_BUFFER, source.impl);
				glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
				glPixelStorei(GL_UNPACK_ROW_LENGTH,
					command.source_layout.bytes_per_row / format.block_bytes * format.block_width);
				glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, command.source_layout.rows_per_image);
				if (format.compressed && HasOpenGLCompressedPixelStorage()) {
					glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_WIDTH, format.block_width);
					glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_HEIGHT, format.block_height);
					glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_DEPTH, 1);
					glPixelStorei(GL_UNPACK_COMPRESSED_BLOCK_SIZE, format.block_bytes);
				}
				auto data = reinterpret_cast<void const*>(command.source_layout.offset);
				auto z = destination.target == GL_TEXTURE_3D
					? region.offset_z : region.base_array_layer;
				auto depth = destination.target == GL_TEXTURE_3D
					? region.depth : region.array_layer_count;
				auto size = static_cast<GLsizei>(OpenGLTransferSize(
					command.source_layout, region, format));
				bool direct_state_access = GLAD_GL_VERSION_4_5 || GLAD_GL_ARB_direct_state_access;
				if (!direct_state_access) {
					glBindTexture(destination.target, destination.impl);
				}
				if (format.compressed && direct_state_access) {
					if (destination.target == GL_TEXTURE_1D) {
						glCompressedTextureSubImage1D(destination.impl, region.mip_level,
							region.offset_x, region.width, destination.format, size, data);
					}
					else if (destination.target == GL_TEXTURE_2D) {
						glCompressedTextureSubImage2D(destination.impl, region.mip_level,
							region.offset_x, region.offset_y, region.width, region.height,
							destination.format, size, data);
					}
					else {
						glCompressedTextureSubImage3D(destination.impl, region.mip_level,
							region.offset_x, region.offset_y, z, region.width, region.height,
							depth, destination.format, size, data);
					}
				}
				else if (format.compressed) {
					if (destination.target == GL_TEXTURE_CUBE_MAP) {
						auto layer_size = static_cast<GLsizei>(command.source_layout.bytes_per_row *
							((region.height + format.block_height - 1u) / format.block_height));
						for (std::uint32_t layer = 0u; layer < region.array_layer_count; ++layer) {
							glCompressedTexSubImage2D(
								GL_TEXTURE_CUBE_MAP_POSITIVE_X + region.base_array_layer + layer,
								region.mip_level, region.offset_x, region.offset_y,
								region.width, region.height, destination.format, layer_size,
								reinterpret_cast<void const*>(command.source_layout.offset +
									static_cast<std::size_t>(layer) * layer_size)
							);
						}
					}
					else if (destination.target == GL_TEXTURE_2D) {
						glCompressedTexSubImage2D(destination.target, region.mip_level,
							region.offset_x, region.offset_y, region.width, region.height,
							destination.format, size, data);
					}
					else {
						glCompressedTexSubImage3D(destination.target, region.mip_level,
							region.offset_x, region.offset_y, z, region.width, region.height,
							depth, destination.format, size, data);
					}
				}
				else if (direct_state_access && destination.target == GL_TEXTURE_1D) {
					glTextureSubImage1D(destination.impl, region.mip_level, region.offset_x,
						region.width, format.format, format.type, data);
				}
				else if (direct_state_access && destination.target == GL_TEXTURE_2D) {
					glTextureSubImage2D(destination.impl, region.mip_level,
						region.offset_x, region.offset_y, region.width, region.height,
						format.format, format.type, data);
				}
				else if (direct_state_access) {
					glTextureSubImage3D(destination.impl, region.mip_level,
						region.offset_x, region.offset_y, z, region.width, region.height, depth,
						format.format, format.type, data);
				}
				else if (destination.target == GL_TEXTURE_CUBE_MAP) {
					auto layer_stride = static_cast<std::size_t>(command.source_layout.bytes_per_row) *
						command.source_layout.rows_per_image;
					for (std::uint32_t layer = 0u; layer < region.array_layer_count; ++layer) {
						glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + region.base_array_layer + layer,
							region.mip_level, region.offset_x, region.offset_y,
							region.width, region.height, format.format, format.type,
							reinterpret_cast<void const*>(command.source_layout.offset +
								static_cast<std::size_t>(layer) * layer_stride));
					}
				}
				else if (destination.target == GL_TEXTURE_2D) {
					glTexSubImage2D(destination.target, region.mip_level,
						region.offset_x, region.offset_y, region.width, region.height,
						format.format, format.type, data);
				}
				else {
					glTexSubImage3D(destination.target, region.mip_level,
						region.offset_x, region.offset_y, z, region.width, region.height, depth,
						format.format, format.type, data);
				}
				if (!direct_state_access) {
					glBindTexture(destination.target, 0u);
				}
				(void)pixel_store;
			}

			void operator()(execution::CopyTextureToBufferCommand const& command) {
				auto const& source = snapshot->resources[command.source.value];
				auto const& destination = snapshot->resources[command.destination.value];
				auto const& region = command.source_region;
				auto format = GetOpenGLTransferFormat(source.format);
				ValidateOpenGLTransferLayout(command.destination_layout, region, format,
					destination.size,
					"OpenGL texture-to-buffer copy exceeds the destination buffer range");
				auto block_columns = (region.width + format.block_width - 1u) /
					format.block_width;
				if (format.compressed && !HasOpenGLCompressedPixelStorage() &&
					(command.destination_layout.bytes_per_row != block_columns * format.block_bytes ||
						command.destination_layout.rows_per_image != region.height)) {
					throw std::invalid_argument(
						"OpenGL compressed readback row pitch requires compressed pixel storage"
					);
				}
				OpenGLPixelPackScope pixel_store;
				glBindBuffer(GL_PIXEL_PACK_BUFFER, destination.impl);
				glPixelStorei(GL_PACK_ALIGNMENT, 1);
				glPixelStorei(GL_PACK_ROW_LENGTH,
					command.destination_layout.bytes_per_row / format.block_bytes * format.block_width);
				glPixelStorei(GL_PACK_IMAGE_HEIGHT, command.destination_layout.rows_per_image);
				if (format.compressed && HasOpenGLCompressedPixelStorage()) {
					glPixelStorei(GL_PACK_COMPRESSED_BLOCK_WIDTH, format.block_width);
					glPixelStorei(GL_PACK_COMPRESSED_BLOCK_HEIGHT, format.block_height);
					glPixelStorei(GL_PACK_COMPRESSED_BLOCK_DEPTH, 1);
					glPixelStorei(GL_PACK_COMPRESSED_BLOCK_SIZE, format.block_bytes);
				}
				auto data = reinterpret_cast<void*>(command.destination_layout.offset);
				auto z = source.target == GL_TEXTURE_3D ? region.offset_z : region.base_array_layer;
				auto depth = source.target == GL_TEXTURE_3D ? region.depth : region.array_layer_count;
				auto size = static_cast<GLsizei>(OpenGLTransferSize(
					command.destination_layout, region, format));
				if (format.compressed) {
					if (!glGetCompressedTextureSubImage) {
						throw std::runtime_error(
							"OpenGL ES cannot read back compressed texture blocks"
						);
					}
					glGetCompressedTextureSubImage(source.impl, region.mip_level,
						region.offset_x, region.offset_y, z, region.width, region.height, depth,
						size, data);
				}
				else if (glGetTextureSubImage) {
					glGetTextureSubImage(source.impl, region.mip_level,
						region.offset_x, region.offset_y, z, region.width, region.height, depth,
						format.format, format.type, size, data);
				}
				else {
					GLuint readback_framebuffer;
					glGenFramebuffers(1u, &readback_framebuffer);
					glBindFramebuffer(GL_READ_FRAMEBUFFER, readback_framebuffer);
					auto layer_stride = static_cast<std::size_t>(command.destination_layout.bytes_per_row) *
						command.destination_layout.rows_per_image;
					for (std::uint32_t layer = 0u; layer < depth; ++layer) {
						if (source.target == GL_TEXTURE_CUBE_MAP) {
							glFramebufferTexture2D(GL_READ_FRAMEBUFFER, format.attachment,
								GL_TEXTURE_CUBE_MAP_POSITIVE_X + z + layer,
								source.impl, region.mip_level);
						}
						else if (source.target == GL_TEXTURE_2D) {
							glFramebufferTexture2D(GL_READ_FRAMEBUFFER, format.attachment,
								GL_TEXTURE_2D, source.impl, region.mip_level);
						}
						else if (source.target == GL_TEXTURE_1D) {
							glFramebufferTexture1D(GL_READ_FRAMEBUFFER, format.attachment,
								GL_TEXTURE_1D, source.impl, region.mip_level);
						}
						else {
							glFramebufferTextureLayer(GL_READ_FRAMEBUFFER, format.attachment,
								source.impl, region.mip_level, z + layer);
						}
						if (format.attachment != GL_COLOR_ATTACHMENT0) {
							glReadBuffer(GL_NONE);
						}
						else {
							glReadBuffer(GL_COLOR_ATTACHMENT0);
						}
						if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
							glDeleteFramebuffers(1u, &readback_framebuffer);
							throw std::runtime_error("OpenGL texture readback framebuffer is incomplete");
						}
						glReadPixels(region.offset_x, region.offset_y, region.width, region.height,
							format.format, format.type,
							reinterpret_cast<void*>(command.destination_layout.offset +
								static_cast<std::size_t>(layer) * layer_stride));
					}
					glDeleteFramebuffers(1u, &readback_framebuffer);
				}
				(void)pixel_store;
			}

			void operator()(execution::CopyTextureCommand const& command) {
				auto const& source = snapshot->resources[command.source.value];
				auto const& destination = snapshot->resources[command.destination.value];
				auto const& source_region = command.source_region;
				auto const& destination_region = command.destination_region;
				if (!glCopyImageSubData) {
					throw std::runtime_error("OpenGL texture copy requires copy-image support");
				}
				glCopyImageSubData(
					source.impl, source.target, source_region.mip_level,
					source_region.offset_x, source_region.offset_y,
					source.target == GL_TEXTURE_3D ? source_region.offset_z : source_region.base_array_layer,
					destination.impl, destination.target, destination_region.mip_level,
					destination_region.offset_x, destination_region.offset_y,
					destination.target == GL_TEXTURE_3D
						? destination_region.offset_z : destination_region.base_array_layer,
					source_region.width, source_region.height,
					source.target == GL_TEXTURE_3D
						? source_region.depth : source_region.array_layer_count
				);
			}

			void operator()(execution::PresentCommand const& command) {
				auto const& source = snapshot->resources[command.source.value];
				if (source.type != Backend::GLResource::Type::Texture) {
					throw std::invalid_argument("OpenGL presentation source must be a texture");
				}
				auto const& target = snapshot->presentation_targets[command.target.value];
				auto CreateEntry = [&target, this]() {
					Backend::PresentationEntry result;
					result.target = target;
					result.frames = std::make_shared<Backend::PresentationEntry::FrameState>();
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
				if (presentation.Get().frames->swap_interval != static_cast<int>(command.vertical_sync) &&
					wglSwapIntervalEXT) {
					wglSwapIntervalEXT(command.vertical_sync ? 1 : 0);
				}
#elif defined(__linux__)
				auto ConfigureSwapInterval = [this, &target, &presentation, &command](auto const& native_target) {
					using Target = std::remove_cvref_t<decltype(native_target)>;
					if (presentation.Get().frames->swap_interval == static_cast<int>(command.vertical_sync)) {
						return;
					}
					if constexpr (std::same_as<Target, Backend::X11PresentationTarget>) {
						if (glXSwapIntervalEXT) {
							glXSwapIntervalEXT(
								native_target.display,
								native_target.window,
								command.vertical_sync ? 1 : 0
							);
						}
						else if (glXSwapIntervalMESA) {
							glXSwapIntervalMESA(command.vertical_sync ? 1u : 0u);
						}
						else if (command.vertical_sync && glXSwapIntervalSGI) {
							glXSwapIntervalSGI(1);
						}
					}
					else {
						auto const& egl = std::get<Backend::Instance::EGL>(
							logical_device->instance->gl_handle
						);
						eglSwapInterval(egl.display, command.vertical_sync ? 1 : 0);
					}
					};
				std::visit(ConfigureSwapInterval, target);
#elif defined(__ANDROID__)
				if (presentation.Get().frames->swap_interval != static_cast<int>(command.vertical_sync)) {
					auto const& egl = std::get<Backend::Instance::EGL>(
						logical_device->instance->gl_handle
					);
					eglSwapInterval(egl.display, command.vertical_sync ? 1 : 0);
				}
#endif
				auto& frame_sync = AcquirePresentationFrame(
					presentation.Get(),
					command.frames_in_flight
				);
				presentation.Get().frames->swap_interval = static_cast<int>(command.vertical_sync);
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
				frame_sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0u);
				if (!frame_sync) {
					throw std::runtime_error("OpenGL could not create a presentation frame fence");
				}
				glFlush();
#if defined(_WIN32)
				wglMakeCurrent(previous_dc, context);
#endif
			}
		};

		GLsync ExecuteOpenGLGraph(void* operation_state) {
			auto& execution = *static_cast<Backend::GraphExecution*>(operation_state);
			auto const& bindings = execution.graph->impl->bindings;
			auto const& snapshot = *static_cast<OpenGLBindingsSnapshot const*>(
				execution.binding_snapshot.get()
			);
			if (execution.batches.empty()) {
				return nullptr;
			}
			GLuint vertex_array = 0u;
			glGenVertexArrays(1u, &vertex_array);
			OpenGLCommandRecorder recorder{
				&bindings,
				&snapshot,
				&execution.batches.front().queue->impl
			};
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
			glDeleteVertexArrays(1u, &vertex_array);
			auto sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0u);
			if (!sync) {
				throw std::runtime_error("OpenGL could not create a graph completion fence");
			}
			glFlush();
			return sync;
		}
	}

namespace fyuu_rhi::opengl {

	Backend::ExecutableGraph Backend::CompileCommandGraph(Backend::CommandGraph const& graph) {
		return execution::MakeExecutableGraph<Backend>(graph);
	}

	Backend::GraphExecution CreateGraphExecution(
		Backend::Scheduler const& scheduler,
		Backend::ExecutableGraph const& graph
	) {
		Backend::GraphExecution result{ scheduler, graph };
		auto const& native_graph = *graph->impl;
		result.binding_snapshot = CaptureBindings(native_graph.bindings);
		result.batches.reserve(graph->plan.batches.size());
		for (auto const& batch_plan : graph->plan.batches) {
			auto const& queue = scheduler->queues.Select(batch_plan.queue_flags);
			auto commands = queue->command_pool->Acquire(CreateCommandList, ResetCommandList);
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

	void StartSchedulerExecution(
		Backend::Scheduler const& scheduler,
		execution::SchedulerCompletion const& completion
	) {
		auto queue = scheduler->queues.graphics;
		if (!queue) queue = scheduler->queues.compute;
		if (!queue) queue = scheduler->queues.copy;
		if (!queue) {
			throw std::logic_error("OpenGL scheduler has no execution queue");
		}
		queue->Enqueue({
			.operation_state = nullptr,
			.Execute = ExecuteSchedule,
			.completion = completion,
			.keep_alive = queue
		});
	}

	void StartDeferredDestroy(
		Backend::Scheduler const& scheduler,
		execution::DeferredDestroy const& deferred_destroy
	) {
		auto queue = scheduler->queues.graphics;
		if (!queue) queue = scheduler->queues.compute;
		if (!queue) queue = scheduler->queues.copy;
		if (!queue) {
			throw std::logic_error("OpenGL scheduler has no execution queue");
		}
		queue->Enqueue({
			.operation_state = const_cast<execution::DeferredDestroy*>(&deferred_destroy),
			.Execute = ExecuteDeferredDestroy,
			.completion = deferred_destroy.completion,
			.keep_alive = queue
		});
	}

	void StartMapResource(
		Backend::Scheduler const& scheduler,
		Backend::Resource& resource,
		execution::ResourceMapRequest const& request
	) {
		auto queue = scheduler->queues.copy;
		if (!queue) queue = scheduler->queues.graphics;
		if (!queue) queue = scheduler->queues.compute;
		if (!queue) {
			throw std::logic_error("OpenGL scheduler has no execution queue");
		}
		auto operation = std::make_shared<OpenGLMapOperation>(
			&resource,
			&request
		);
		queue->Enqueue({
			.operation_state = operation.get(),
			.Execute = ExecuteMapResource,
			.completion = {
				.operation = operation.get(),
				.SetValue = nullptr,
				.SetError = CompleteMapError,
				.SetStopped = nullptr
			},
			.keep_alive = operation
		});
	}

	void StartUnmapResource(
		Backend::Scheduler const& scheduler,
		Backend::Resource& resource,
		execution::ResourceUnmapRequest const& request
	) {
		auto queue = scheduler->queues.copy;
		if (!queue) queue = scheduler->queues.graphics;
		if (!queue) queue = scheduler->queues.compute;
		if (!queue) {
			throw std::logic_error("OpenGL scheduler has no execution queue");
		}
		auto operation = std::make_shared<OpenGLUnmapOperation>(&resource);
		queue->Enqueue({
			.operation_state = operation.get(),
			.Execute = ExecuteUnmapResource,
			.completion = request.completion,
			.keep_alive = operation
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
			std::optional<Submission> submission;
			{
				std::unique_lock<std::mutex> lock(self->mutex);
				auto HasSubmissionOrStopped = [self, stop_token]() noexcept {
					return !self->submissions.empty() || stop_token.stop_requested();
				};
				if (self->pending_completions.empty()) {
					self->condition.wait(lock, HasSubmissionOrStopped);
				}
				else {
					self->condition.wait_for(
						lock,
						std::chrono::milliseconds(1u),
						HasSubmissionOrStopped
					);
				}
				if (!self->submissions.empty()) {
					submission = self->submissions.front();
					self->submissions.pop_front();
				}
				else if (stop_token.stop_requested()) {
					for (auto& pending : self->pending_completions) {
						glDeleteSync(pending.sync);
						if (pending.completion.SetStopped) {
							pending.completion.SetStopped(pending.completion.operation);
						}
					}
					self->pending_completions.clear();
					break;
				}
			}
			if (submission) {
				try {
					auto sync = submission->Execute(submission->operation_state);
					if (sync) {
						self->pending_completions.push_back({
							sync,
							submission->completion,
							submission->keep_alive
						});
					}
					else {
						auto CompleteValue = [
							completion = submission->completion,
							keep_alive = submission->keep_alive
						]() noexcept {
							(void)keep_alive;
							if (completion.SetValue) {
								completion.SetValue(completion.operation);
							}
						};
						self->impl.completion_service->Enqueue(PollCompleted, CompleteValue);
					}
				}
				catch (...) {
					auto error = std::current_exception();
					auto CompleteError = [
						completion = submission->completion,
						keep_alive = submission->keep_alive,
						error
					]() noexcept {
						(void)keep_alive;
						completion.SetError(completion.operation, error);
					};
					self->impl.completion_service->Enqueue(PollCompleted, CompleteError);
				}
			}

			while (!self->pending_completions.empty()) {
				auto& pending = self->pending_completions.front();
				auto status = glClientWaitSync(pending.sync, 0u, 0u);
				if (status == GL_TIMEOUT_EXPIRED) {
					break;
				}
				glDeleteSync(pending.sync);
				if (status == GL_WAIT_FAILED) {
					std::exception_ptr error;
					try {
						throw std::runtime_error("OpenGL graph completion fence wait failed");
					}
					catch (...) {
						error = std::current_exception();
					}
					auto CompleteError = [
						completion = pending.completion,
						keep_alive = pending.keep_alive,
						error
					]() noexcept {
						(void)keep_alive;
						completion.SetError(completion.operation, error);
					};
					self->impl.completion_service->Enqueue(PollCompleted, CompleteError);
				}
				else {
					auto CompleteValue = [
						completion = pending.completion,
						keep_alive = pending.keep_alive
					]() noexcept {
						(void)keep_alive;
						if (completion.SetValue) {
							completion.SetValue(completion.operation);
						}
					};
					self->impl.completion_service->Enqueue(PollCompleted, CompleteValue);
				}
				self->pending_completions.pop_front();
			}
		}
	}

}
#endif // !defined(__APPLE__)
