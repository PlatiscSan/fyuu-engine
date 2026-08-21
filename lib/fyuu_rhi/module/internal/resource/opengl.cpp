module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <utility>

#include <optional>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#include <glad/glad.h>
#endif // !defined(__APPLE__)

module fyuu_rhi:opengl_resource;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :opengl_data;
import :opengl_utility;
import :resource_dispatch;
import :view_factory;

namespace fyuu_rhi::opengl {

	Resource::Resource(GLuint buffer) noexcept
		: impl(buffer, ResourceDeleter{ ResourceType::Buffer }),
		target(0u),
		format(0u),
		type(ResourceType::Buffer) {
	}

	Resource::Resource(GLuint texture, GLenum target_, GLenum format_) noexcept
		: impl(texture, ResourceDeleter{ ResourceType::Texture }),
		target(target_),
		format(format_),
		type(ResourceType::Texture) {
	}

	void ResourceDeleter::operator()(GLuint impl) const noexcept {
		if (impl == 0u) {
			return;
		}
		if (type == ResourceType::Buffer) {
			glDeleteBuffers(1, &impl);
		}
		else {
			glDeleteTextures(1, &impl);
		}
	}

	View::View(
		GLuint impl_,
		std::optional<View::BufferRange> buffer_range_,
		GLenum target_,
		GLenum format_,
		bool owned_
	) noexcept
		: impl(impl_, ViewDeleter{ owned_ }),
		buffer_range(buffer_range_),
		target(target_),
		format(format_) {
	}

	void ViewDeleter::operator()(GLuint impl) const noexcept {
		if (owned && impl != 0u) {
			glDeleteTextures(1, &impl);
		}
	}

} // namespace fyuu_rhi::opengl

namespace fyuu_rhi {

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
						resource->impl.get(),
						static_cast<GLintptr>(offset),
						static_cast<GLsizeiptr>(range)
					);
				}
				else {
					glTextureBuffer(view, internal_format, resource->impl.get());
				}
			}
			else {
				glBindTexture(target, view);
				if (range_used) {
					glTexBufferRange(
						target,
						internal_format,
						resource->impl.get(),
						static_cast<GLintptr>(offset),
						static_cast<GLsizeiptr>(range)
					);
				}
				else {
					glTexBuffer(target, internal_format, resource->impl.get());
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
					internal_format,
					true
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
			// Aliasing a 2D render target is the common whole-texture case; the view
			// borrows the source texture and does not own it.
			if (whole_texture &&
				flags.Test(ResourceFlagBits::RenderAttachment) &&
				resource->target == GL_TEXTURE_2D) {
				return MakeView(
					opengl::View(
						resource->impl.get(),
						std::nullopt,
						view_target,
						internal_format,
						false
					)
				);
			}
			if (!GLAD_GL_ARB_texture_view) {
				// Without glTextureView, a whole-texture view can still alias; any
				// subresource view is impossible.
				if (whole_texture) {
					return MakeView(
						opengl::View(
							resource->impl.get(),
							std::nullopt,
							view_target,
							internal_format,
							false
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
				resource->impl.get(),
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
					internal_format,
					true
				)
			);
		}
	};

} // namespace fyuu_rhi
#endif // !defined(__APPLE__)
