module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#include "glad/glad.h"

#if defined(_WIN32)
#include "glad/glad_wgl.h"
#elif defined(__linux__)
#include "glad/glad_glx.h"
#include "glad/glad_egl.h"
#elif defined(__ANDROID__)
#include "glad/glad_egl.h"
#endif // defined(_WIN32)
#endif // !defined(__APPLE__)

module fyuu_rhi:opengl_resource;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :opengl_traits;
import :pipeline_types;

namespace fyuu_rhi::opengl {

	Backend::Resource::Resource(GLuint impl_, Type type_, std::size_t size_) noexcept
		: impl(impl_), size(size_), type(type_) {}

	Backend::Resource::Resource(
		GLuint impl_,
		GLenum target_,
		GLenum format_,
		std::uint32_t width_,
		std::uint32_t height_
	) noexcept : impl(impl_),
		target(target_),
		format(format_),
		width(width_),
		height(height_),
		type(Type::Texture) {}

	Backend::Resource::Resource(Resource&& other) noexcept
		: impl(std::exchange(other.impl, 0)),
		target(other.target),
		format(other.format),
		size(other.size),
		width(other.width),
		height(other.height),
		type(other.type) {}

	Backend::Resource& Backend::Resource::operator=(Resource&& other) noexcept {
		std::swap(impl, other.impl);
		std::swap(target, other.target);
		std::swap(format, other.format);
		std::swap(size, other.size);
		std::swap(width, other.width);
		std::swap(height, other.height);
		type = other.type;
		return *this;
	}

	Backend::Resource::~Resource() noexcept {
		switch (type) {
		case Type::Buffer:
			glDeleteBuffers(1u, &impl);
			break;
		case Type::Texture:
			glDeleteTextures(1u, &impl);
			break;
		default:
			break;
		}
	}

	Backend::GLTextureView::GLTextureView(
		GLuint impl_,
		GLenum target_,
		GLenum format_,
		bool owned_
	) noexcept : impl(impl_),
		target(target_),
		format(format_),
		owned(owned_) {}

	Backend::GLTextureView::GLTextureView(GLTextureView&& other) noexcept
		: impl(std::exchange(other.impl, 0)),
		target(other.target),
		format(other.format),
		owned(std::exchange(other.owned, false)) {}

	Backend::GLTextureView& Backend::GLTextureView::operator=(GLTextureView&& other) noexcept {
		std::swap(impl, other.impl);
		std::swap(target, other.target);
		std::swap(format, other.format);
		std::swap(owned, other.owned);
		return *this;
	}

	Backend::GLTextureView::~GLTextureView() noexcept {
		if (owned) {
			glDeleteTextures(1u, &impl);
		}
	}

	Backend::GLBufferView::GLBufferView(GLuint impl_, std::optional<Range> range_) noexcept
		: impl(impl_), range(std::move(range_)) {}

	Backend::GLBufferView::GLBufferView(GLBufferView&& other) noexcept
		: impl(std::exchange(other.impl, 0)), range(std::move(other.range)) {}

	Backend::GLBufferView& Backend::GLBufferView::operator=(GLBufferView&& other) noexcept {
		std::swap(impl, other.impl);
		range = std::move(other.range);
		return *this;
	}

	Backend::GLBufferView::~GLBufferView() noexcept {
		glDeleteTextures(1u, &impl);
	}

	Backend::Sampler::Sampler(GLuint impl_) noexcept : impl(impl_) {}

	Backend::Sampler::Sampler(Sampler&& other) noexcept : impl(std::exchange(other.impl, 0)) {}

	Backend::Sampler& Backend::Sampler::operator=(Sampler&& other) noexcept {
		std::swap(impl, other.impl);
		return *this;
	}

	Backend::Sampler::~Sampler() noexcept {
		if (impl) {
			glDeleteSamplers(1u, &impl);
		}
	}

	Backend::Pipeline::Pipeline(
		GLuint impl_,
		std::vector<VertexBufferLayout> vertex_buffers_,
		std::vector<VertexAttribute> vertex_attributes_,
		PrimitiveState primitive_,
		RasterizationState rasterization_,
		MultisampleState multisample_,
		std::optional<DepthStencilState> depth_stencil_,
		std::vector<ColorTargetState> color_targets_,
		std::vector<PipelineBindingMetadata> bindings_
	) : impl(impl_),
		vertex_buffers(std::move(vertex_buffers_)),
		vertex_attributes(std::move(vertex_attributes_)),
		primitive(primitive_),
		rasterization(rasterization_),
		multisample(multisample_),
		depth_stencil(std::move(depth_stencil_)),
		color_targets(std::move(color_targets_)),
		bindings(std::move(bindings_)) {}

	Backend::Pipeline::Pipeline(GLuint impl_, std::vector<PipelineBindingMetadata> bindings_)
		: impl(impl_), compute(true), bindings(std::move(bindings_)) {}

	Backend::Pipeline::Pipeline(Pipeline&& other) noexcept
		: impl(std::exchange(other.impl, 0)),
		compute(other.compute),
		vertex_buffers(std::move(other.vertex_buffers)),
		vertex_attributes(std::move(other.vertex_attributes)),
		primitive(other.primitive),
		rasterization(other.rasterization),
		multisample(other.multisample),
		depth_stencil(std::move(other.depth_stencil)),
		color_targets(std::move(other.color_targets)),
		bindings(std::move(other.bindings)) {}

	Backend::Pipeline& Backend::Pipeline::operator=(Pipeline&& other) noexcept {
		std::swap(impl, other.impl);
		std::swap(compute, other.compute);
		vertex_buffers = std::move(other.vertex_buffers);
		vertex_attributes = std::move(other.vertex_attributes);
		primitive = other.primitive;
		rasterization = other.rasterization;
		multisample = other.multisample;
		depth_stencil = std::move(other.depth_stencil);
		color_targets = std::move(other.color_targets);
		bindings = std::move(other.bindings);
		return *this;
	}

	Backend::Pipeline::~Pipeline() noexcept {
		if (impl) {
			glDeleteProgram(impl);
		}
	}

} // namespace fyuu_rhi::opengl
#endif // !defined(__APPLE__)
