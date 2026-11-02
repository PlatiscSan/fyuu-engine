module;
#if !defined(__EMSCRIPTEN__)
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <utility>

#include <optional>

#include <format>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#include <glad/glad.h>
#endif // !defined(__APPLE__)

#endif // !defined(__EMSCRIPTEN__)

module fyuu_rhi:opengl_resource;
#if !defined(__EMSCRIPTEN__)
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core;
import :instance;
import :opengl_data;
import :opengl_utility;
import :resource_dispatch;
import :memory_pressure;
import :view_factory;

namespace fyuu_rhi::opengl {
	void SyncDeleter::operator()(GLsync sync) const noexcept {
		RetireSync(sync);
	}
} // namespace fyuu_rhi::opengl

namespace fyuu_rhi {
	template <>
	struct MapResource<opengl::Resource> {
		opengl::Resource* resource;

		static void Unmap(void* context, ResourceDataRange, bool) noexcept {
			auto native = static_cast<opengl::Resource*>(context);
			(void)glUnmapNamedBuffer(native->impl->get());
		}

		ResourceMapScope operator()(ResourceDataRange range, bool writable) const {
			/** @brief Mapping is valid on completion and application threads through a per-thread shared context. */
			RequestInstance(Backend::OpenGL).ShareContextOnThisThread();
			if (resource->type != opengl::ResourceType::Buffer) {
				throw std::invalid_argument("An OpenGL texture cannot be mapped directly");
			}
			auto data = glMapNamedBufferRange(
				resource->impl->get(),
				static_cast<GLintptr>(range.offset),
				static_cast<GLsizeiptr>(range.size),
				writable ? GL_MAP_WRITE_BIT : GL_MAP_READ_BIT
			);
			if (!data) {
				throw std::runtime_error("Failed to map an OpenGL readback buffer");
			}
			return ResourceMapScope(
				resource,
				static_cast<std::byte*>(data),
				range.offset,
				range.size,
				writable,
				Unmap
			);
		}
	};

	template <>
	struct CreateBufferView<opengl::Resource> {
		opengl::Resource* resource;

		View operator()(std::size_t offset, std::size_t range, ResourceFlags const& flags) const {
			if (resource->type != opengl::ResourceType::Buffer) {
				throw std::invalid_argument("An OpenGL buffer view requires a buffer resource");
			}
			static constexpr GLenum target = GL_TEXTURE_BUFFER;
			if (!GLAD_GL_ARB_texture_buffer_object) {
				throw std::runtime_error(
					"OpenGL buffer views require GL_ARB_texture_buffer_object"
				);
			}
			GLuint view = 0u;
			if (GLAD_GL_ARB_direct_state_access) {
				glCreateTextures(target, 1u, &view);
			}
			else {
				glGenTextures(1, &view);
			}
			if (view == 0u) {
				throw std::runtime_error("Failed to create an OpenGL texture buffer view");
			}
			GLenum internal_format = opengl::InternalFormat(flags);
			bool range_used = GLAD_GL_ARB_texture_buffer_range &&
				(offset != 0u || range != 0u);
			if (GLAD_GL_ARB_direct_state_access) {
				if (range_used) {
					glTextureBufferRange(
						view,
						internal_format,
						resource->impl->get(),
						static_cast<GLintptr>(offset),
						static_cast<GLsizeiptr>(range)
					);
				}
				else {
					glTextureBuffer(view, internal_format, resource->impl->get());
				}
			}
			else {
				glBindTexture(target, view);
				if (range_used) {
					glTexBufferRange(
						target,
						internal_format,
						resource->impl->get(),
						static_cast<GLintptr>(offset),
						static_cast<GLsizeiptr>(range)
					);
				}
				else {
					glTexBuffer(target, internal_format, resource->impl->get());
				}
				glBindTexture(target, 0u);
			}
			std::optional<opengl::View::BufferRange> buffer_range;
			if (!GLAD_GL_ARB_texture_buffer_range) {
				// Fallback to the whole-buffer binding; record the requested window
				// so the execution path can rebind the range when needed.
				buffer_range = opengl::View::BufferRange{ offset, range };
			}
			return MakeView(
				opengl::View(
					view,
					std::move(buffer_range),
					target,
					internal_format
				)
			);
		}
	};

	template <>
	struct CreateTextureView<opengl::Resource> {
		opengl::Resource* resource;

		View operator()(
			std::size_t base_mip_lvl,
			std::size_t mip_lvl_cnt,
			std::size_t base_arr_layer,
			std::size_t arr_layer_cnt,
			ResourceFlags const& flags
		) const {
			if (resource->type != opengl::ResourceType::Texture) {
				throw std::invalid_argument("An OpenGL texture view requires a texture resource");
			}
			GLenum view_target = opengl::TextureViewTarget(flags);
			GLenum internal_format = opengl::InternalFormat(flags);
			bool whole_texture =
				base_mip_lvl == 0u && mip_lvl_cnt == 1u &&
				base_arr_layer == 0u && arr_layer_cnt == 1u &&
				view_target == resource->target && internal_format == resource->format;
			/** @brief Whole-texture aliases share the source texture owner. */
			if (whole_texture &&
				flags.Test(ResourceFlagBits::RenderAttachment) &&
				resource->target == GL_TEXTURE_2D) {
				return MakeView(
					opengl::View(
						resource->impl,
						std::nullopt,
						view_target,
						internal_format
					)
				);
			}
			if (!GLAD_GL_ARB_texture_view) {
				// Without glTextureView, a whole-texture view can still alias; any
				// subresource view is impossible.
				if (whole_texture) {
					return MakeView(
						opengl::View(
							resource->impl,
							std::nullopt,
							view_target,
							internal_format
						)
					);
				}
				throw std::runtime_error(
					"OpenGL subresource views require GL_ARB_texture_view"
				);
			}
			GLuint view = 0u;
			glGenTextures(1u, &view);
			if (view == 0u) {
				throw std::runtime_error("Failed to create an OpenGL texture view");
			}
			glTextureView(
				view,
				view_target,
				resource->impl->get(),
				internal_format,
				static_cast<GLuint>(base_mip_lvl),
				static_cast<GLuint>(mip_lvl_cnt),
				static_cast<GLuint>(base_arr_layer),
				static_cast<GLuint>(arr_layer_cnt)
			);
			return MakeView(
				opengl::View(
					view,
					std::nullopt,
					view_target,
					internal_format
				)
			);
		}
	};

} // namespace fyuu_rhi
#endif // !defined(__APPLE__)
#endif // !defined(__EMSCRIPTEN__)
