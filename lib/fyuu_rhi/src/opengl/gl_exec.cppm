module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#include "log.hpp"

#include "glad/glad.h"
#if defined(_WIN32)
#include "glad/glad_wgl.h"
#include <windows.h>
#elif defined(__linux__)
#include "glad/glad_glx.h"
#include "glad/glad_egl.h"
#elif defined(__ANDROID__)
#include "glad/glad_egl.h"
#include <android/native_window.h>
#endif // defined(_WIN32)
#endif // !defined(__APPLE__)

module fyuu_rhi:opengl_execution;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :opengl_traits;
import :execution_types;
import :resource_types;
import :pipeline_types;
import :native_pipeline_binding;
import :log;

namespace {

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::pipeline;
	using namespace fyuu_rhi::execution;
	using fyuu_rhi::opengl::Backend;
	using Submission = Backend::Submission;

	/// Vertex attribute storage class: float/normalized use glVertexArrayAttribFormat,
	/// integer formats must use glVertexArrayAttribIFormat or the shader reads garbage.
	enum class AttributeClass : std::uint8_t { Float, Normalized, Integer };

	/// Maps a vertex attribute format to (size, type, class).
	void AttributeFormat(
		ResourceFlagBits format,
		GLint& size,
		GLenum& type,
		AttributeClass& attribute_class
	) {
		switch (format) {
		case ResourceFlagBits::R32Float: size = 1; type = GL_FLOAT; attribute_class = AttributeClass::Float; return;
		case ResourceFlagBits::R32G32Float: size = 2; type = GL_FLOAT; attribute_class = AttributeClass::Float; return;
		case ResourceFlagBits::R32G32B32A32Float: size = 4; type = GL_FLOAT; attribute_class = AttributeClass::Float; return;
		case ResourceFlagBits::R16Float: size = 1; type = GL_HALF_FLOAT; attribute_class = AttributeClass::Float; return;
		case ResourceFlagBits::R16G16Float: size = 2; type = GL_HALF_FLOAT; attribute_class = AttributeClass::Float; return;
		case ResourceFlagBits::R16G16B16A16Float: size = 4; type = GL_HALF_FLOAT; attribute_class = AttributeClass::Float; return;
		case ResourceFlagBits::R11G11B10Float: size = 3; type = GL_UNSIGNED_INT_10F_11F_11F_REV; attribute_class = AttributeClass::Float; return;

		case ResourceFlagBits::R8Unorm: size = 1; type = GL_UNSIGNED_BYTE; attribute_class = AttributeClass::Normalized; return;
		case ResourceFlagBits::R8G8Unorm: size = 2; type = GL_UNSIGNED_BYTE; attribute_class = AttributeClass::Normalized; return;
		case ResourceFlagBits::R8G8B8A8Unorm: size = 4; type = GL_UNSIGNED_BYTE; attribute_class = AttributeClass::Normalized; return;
		case ResourceFlagBits::R8G8B8A8Srgb: size = 4; type = GL_UNSIGNED_BYTE; attribute_class = AttributeClass::Normalized; return;
		case ResourceFlagBits::R8Snorm: size = 1; type = GL_BYTE; attribute_class = AttributeClass::Normalized; return;
		case ResourceFlagBits::R8G8Snorm: size = 2; type = GL_BYTE; attribute_class = AttributeClass::Normalized; return;
		case ResourceFlagBits::R8G8B8A8Snorm: size = 4; type = GL_BYTE; attribute_class = AttributeClass::Normalized; return;
		case ResourceFlagBits::R16Unorm: size = 1; type = GL_UNSIGNED_SHORT; attribute_class = AttributeClass::Normalized; return;
		case ResourceFlagBits::R16G16Unorm: size = 2; type = GL_UNSIGNED_SHORT; attribute_class = AttributeClass::Normalized; return;
		case ResourceFlagBits::R16G16B16A16Unorm: size = 4; type = GL_UNSIGNED_SHORT; attribute_class = AttributeClass::Normalized; return;
		case ResourceFlagBits::R16Snorm: size = 1; type = GL_SHORT; attribute_class = AttributeClass::Normalized; return;
		case ResourceFlagBits::R16G16Snorm: size = 2; type = GL_SHORT; attribute_class = AttributeClass::Normalized; return;
		case ResourceFlagBits::R16G16B16A16Snorm: size = 4; type = GL_SHORT; attribute_class = AttributeClass::Normalized; return;
		case ResourceFlagBits::R10G10B10A2Unorm: size = 4; type = GL_UNSIGNED_INT_2_10_10_10_REV; attribute_class = AttributeClass::Normalized; return;

		case ResourceFlagBits::R8Uint: size = 1; type = GL_UNSIGNED_BYTE; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R8G8Uint: size = 2; type = GL_UNSIGNED_BYTE; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R8G8B8A8Uint: size = 4; type = GL_UNSIGNED_BYTE; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R8Sint: size = 1; type = GL_BYTE; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R8G8Sint: size = 2; type = GL_BYTE; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R8G8B8A8Sint: size = 4; type = GL_BYTE; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R16Uint: size = 1; type = GL_UNSIGNED_SHORT; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R16G16Uint: size = 2; type = GL_UNSIGNED_SHORT; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R16G16B16A16Uint: size = 4; type = GL_UNSIGNED_SHORT; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R16Sint: size = 1; type = GL_SHORT; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R16G16Sint: size = 2; type = GL_SHORT; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R16G16B16A16Sint: size = 4; type = GL_SHORT; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R32Uint: size = 1; type = GL_UNSIGNED_INT; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R32G32Uint: size = 2; type = GL_UNSIGNED_INT; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R32G32B32A32Uint: size = 4; type = GL_UNSIGNED_INT; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R32Sint: size = 1; type = GL_INT; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R32G32Sint: size = 2; type = GL_INT; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R32G32B32A32Sint: size = 4; type = GL_INT; attribute_class = AttributeClass::Integer; return;
		case ResourceFlagBits::R10G10B10A2Uint: size = 4; type = GL_UNSIGNED_INT_2_10_10_10_REV; attribute_class = AttributeClass::Integer; return;
		default:
			throw std::invalid_argument("OpenGL vertex attribute format is unsupported");
		}
	}

	GLenum PrimitiveMode(PrimitiveTopology topology) noexcept {
		switch (topology) {
		case PrimitiveTopology::PointList: return GL_POINTS;
		case PrimitiveTopology::LineList: return GL_LINES;
		case PrimitiveTopology::LineStrip: return GL_LINE_STRIP;
		case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
		case PrimitiveTopology::TriangleList: return GL_TRIANGLES;
		}
		return GL_TRIANGLES;
	}

	GLenum NativeCompareOp(CompareOperation value) noexcept {
		switch (value) {
		case CompareOperation::Never: return GL_NEVER;
		case CompareOperation::Less: return GL_LESS;
		case CompareOperation::Equal: return GL_EQUAL;
		case CompareOperation::LessEqual: return GL_LEQUAL;
		case CompareOperation::Greater: return GL_GREATER;
		case CompareOperation::NotEqual: return GL_NOTEQUAL;
		case CompareOperation::GreaterEqual: return GL_GEQUAL;
		case CompareOperation::Always: return GL_ALWAYS;
		}
		return GL_ALWAYS;
	}

	GLenum NativeBlendFactor(BlendFactor value) noexcept {
		switch (value) {
		case BlendFactor::Zero: return GL_ZERO;
		case BlendFactor::One: return GL_ONE;
		case BlendFactor::SourceColor: return GL_SRC_COLOR;
		case BlendFactor::OneMinusSourceColor: return GL_ONE_MINUS_SRC_COLOR;
		case BlendFactor::SourceAlpha: return GL_SRC_ALPHA;
		case BlendFactor::OneMinusSourceAlpha: return GL_ONE_MINUS_SRC_ALPHA;
		case BlendFactor::DestinationColor: return GL_DST_COLOR;
		case BlendFactor::OneMinusDestinationColor: return GL_ONE_MINUS_DST_COLOR;
		case BlendFactor::DestinationAlpha: return GL_DST_ALPHA;
		case BlendFactor::OneMinusDestinationAlpha: return GL_ONE_MINUS_DST_ALPHA;
		case BlendFactor::SourceAlphaSaturated: return GL_SRC_ALPHA_SATURATE;
		case BlendFactor::Constant: return GL_CONSTANT_COLOR;
		case BlendFactor::OneMinusConstant: return GL_ONE_MINUS_CONSTANT_COLOR;
		}
		return GL_ONE;
	}

	GLenum NativeBlendOperation(BlendOperation value) noexcept {
		switch (value) {
		case BlendOperation::Add: return GL_FUNC_ADD;
		case BlendOperation::Subtract: return GL_FUNC_SUBTRACT;
		case BlendOperation::ReverseSubtract: return GL_FUNC_REVERSE_SUBTRACT;
		case BlendOperation::Min: return GL_MIN;
		case BlendOperation::Max: return GL_MAX;
		}
		return GL_FUNC_ADD;
	}

	/// True for internal formats carrying a stencil component.
	bool HasStencil(GLenum internal_format) noexcept {
		return internal_format == GL_DEPTH24_STENCIL8 ||
			internal_format == GL_DEPTH32F_STENCIL8;
	}

	/// External (format, type) pair for pixel-transfer copies. Returns false for
	/// compressed formats, which are rejected by the copy path.
	bool ExternalFormat(GLenum internal_format, GLenum& format, GLenum& type) noexcept {
		switch (internal_format) {
		case GL_R8: format = GL_RED; type = GL_UNSIGNED_BYTE; return true;
		case GL_RG8: format = GL_RG; type = GL_UNSIGNED_BYTE; return true;
		case GL_RGBA8: format = GL_RGBA; type = GL_UNSIGNED_BYTE; return true;
		case GL_SRGB8_ALPHA8: format = GL_RGBA; type = GL_UNSIGNED_BYTE; return true;
		case GL_R8UI: format = GL_RED_INTEGER; type = GL_UNSIGNED_BYTE; return true;
		case GL_RG8UI: format = GL_RG_INTEGER; type = GL_UNSIGNED_BYTE; return true;
		case GL_RGBA8UI: format = GL_RGBA_INTEGER; type = GL_UNSIGNED_BYTE; return true;
		case GL_R8I: format = GL_RED_INTEGER; type = GL_BYTE; return true;
		case GL_RG8I: format = GL_RG_INTEGER; type = GL_BYTE; return true;
		case GL_RGBA8I: format = GL_RGBA_INTEGER; type = GL_BYTE; return true;
		case GL_R16: format = GL_RED; type = GL_UNSIGNED_SHORT; return true;
		case GL_RG16: format = GL_RG; type = GL_UNSIGNED_SHORT; return true;
		case GL_RGBA16: format = GL_RGBA; type = GL_UNSIGNED_SHORT; return true;
		case GL_R16UI: format = GL_RED_INTEGER; type = GL_UNSIGNED_SHORT; return true;
		case GL_RG16UI: format = GL_RG_INTEGER; type = GL_UNSIGNED_SHORT; return true;
		case GL_RGBA16UI: format = GL_RGBA_INTEGER; type = GL_UNSIGNED_SHORT; return true;
		case GL_R16I: format = GL_RED_INTEGER; type = GL_SHORT; return true;
		case GL_RG16I: format = GL_RG_INTEGER; type = GL_SHORT; return true;
		case GL_RGBA16I: format = GL_RGBA_INTEGER; type = GL_SHORT; return true;
		case GL_R16F: format = GL_RED; type = GL_HALF_FLOAT; return true;
		case GL_RG16F: format = GL_RG; type = GL_HALF_FLOAT; return true;
		case GL_RGBA16F: format = GL_RGBA; type = GL_HALF_FLOAT; return true;
		case GL_R32UI: format = GL_RED_INTEGER; type = GL_UNSIGNED_INT; return true;
		case GL_RG32UI: format = GL_RG_INTEGER; type = GL_UNSIGNED_INT; return true;
		case GL_RGBA32UI: format = GL_RGBA_INTEGER; type = GL_UNSIGNED_INT; return true;
		case GL_R32I: format = GL_RED_INTEGER; type = GL_INT; return true;
		case GL_RG32I: format = GL_RG_INTEGER; type = GL_INT; return true;
		case GL_RGBA32I: format = GL_RGBA_INTEGER; type = GL_INT; return true;
		case GL_R32F: format = GL_RED; type = GL_FLOAT; return true;
		case GL_RG32F: format = GL_RG; type = GL_FLOAT; return true;
		case GL_RGBA32F: format = GL_RGBA; type = GL_FLOAT; return true;
		case GL_DEPTH_COMPONENT16: format = GL_DEPTH_COMPONENT; type = GL_UNSIGNED_SHORT; return true;
		case GL_DEPTH_COMPONENT32F: format = GL_DEPTH_COMPONENT; type = GL_FLOAT; return true;
		default: return false;
		}
	}

	/// Byte size of one texel for uncompressed internal formats (0 if unknown).
	GLsizei TexelBytes(GLenum internal_format) noexcept {
		switch (internal_format) {
		case GL_R8: case GL_R8UI: case GL_R8I: return 1;
		case GL_RG8: case GL_RG8UI: case GL_RG8I:
		case GL_R16: case GL_R16UI: case GL_R16I: case GL_R16F:
		case GL_DEPTH_COMPONENT16: return 2;
		case GL_RGBA8: case GL_RGBA8UI: case GL_RGBA8I: case GL_SRGB8_ALPHA8:
		case GL_RG16: case GL_RG16UI: case GL_RG16I: case GL_RG16F:
		case GL_R32UI: case GL_R32I: case GL_R32F:
		case GL_DEPTH_COMPONENT32F: return 4;
		case GL_RGBA16: case GL_RGBA16UI: case GL_RGBA16I: case GL_RGBA16F:
		case GL_RG32UI: case GL_RG32I: case GL_RG32F: return 8;
		case GL_RGBA32UI: case GL_RGBA32I: case GL_RGBA32F: return 16;
		default: return 0;
		}
	}

	/// Bytes per 4x4 block for compressed internal formats (0 if not compressed).
	GLsizei CompressedBlockBytes(GLenum internal_format) noexcept {
		switch (internal_format) {
		case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
		case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
		case GL_COMPRESSED_RED_RGTC1:
		case GL_COMPRESSED_SIGNED_RED_RGTC1:
			return 8;
		case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
		case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
		case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
		case GL_COMPRESSED_RG_RGTC2:
		case GL_COMPRESSED_SIGNED_RG_RGTC2:
		case GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT:
		case GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT:
		case GL_COMPRESSED_RGBA_BPTC_UNORM:
		case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM:
			return 16;
		default:
			return 0;
		}
	}

	/// glMemoryBarrier mask for a resource that was just written with `usage`.
	GLbitfield BarrierMask(ResourceUsage usage) noexcept {
		GLbitfield mask = 0u;
		if (HasUsage(usage, ResourceUsage::Storage)) {
			mask |= GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
		}
		if (HasUsage(usage, ResourceUsage::ColorAttachment) ||
			HasUsage(usage, ResourceUsage::DepthStencilAttachment)) {
			mask |= GL_FRAMEBUFFER_BARRIER_BIT;
		}
		if (HasUsage(usage, ResourceUsage::CopyDestination)) {
			mask |= GL_BUFFER_UPDATE_BARRIER_BIT |
				GL_TEXTURE_UPDATE_BARRIER_BIT |
				GL_PIXEL_BUFFER_BARRIER_BIT;
		}
		if (HasUsage(usage, ResourceUsage::Uniform)) {
			mask |= GL_UNIFORM_BARRIER_BIT;
		}
		if (mask == 0u) {
			mask = GL_ALL_BARRIER_BITS;
		}
		return mask;
	}

	GLenum BufferTarget(ResourceFlags flags) {
		if (flags.Test(ResourceFlagBits::StorageBuffer)) {
			return GL_SHADER_STORAGE_BUFFER;
		}
		return GL_UNIFORM_BUFFER;
	}

	/// Human-readable name of a command record, for error diagnostics.
	char const* CommandName(CommandRecord const& command) {
		return std::visit(
			[](auto const& value) -> char const* {
				using T = std::remove_cvref_t<decltype(value)>;
				if constexpr (std::same_as<T, BeginRendering>) {
					return "BeginRendering";
				}
				else if constexpr (std::same_as<T, EndRendering>) {
					return "EndRendering";
				}
				else if constexpr (std::same_as<T, BindPipeline>) {
					return "BindPipeline";
				}
				else if constexpr (std::same_as<T, BindResourceGroup>) {
					return "BindResourceGroup";
				}
				else if constexpr (std::same_as<T, BindVertexBuffer>) {
					return "BindVertexBuffer";
				}
				else if constexpr (std::same_as<T, BindIndexBuffer>) {
					return "BindIndexBuffer";
				}
				else if constexpr (std::same_as<T, Viewport>) {
					return "Viewport";
				}
				else if constexpr (std::same_as<T, Scissor>) {
					return "Scissor";
				}
				else if constexpr (std::same_as<T, Draw>) {
					return "Draw";
				}
				else if constexpr (std::same_as<T, DrawIndexed>) {
					return "DrawIndexed";
				}
				else if constexpr (std::same_as<T, Dispatch>) {
					return "Dispatch";
				}
				else if constexpr (std::same_as<T, CopyBuffer>) {
					return "CopyBuffer";
				}
				else if constexpr (std::same_as<T, CopyBufferToTexture>) {
					return "CopyBufferToTexture";
				}
				else if constexpr (std::same_as<T, CopyTextureToBuffer>) {
					return "CopyTextureToBuffer";
				}
				else if constexpr (std::same_as<T, CopyTexture>) {
					return "CopyTexture";
				}
				else if constexpr (std::same_as<T, Present>) {
					return "Present";
				}
				else {
					return "Unknown";
				}
			},
			command
		);
	}

	/// The scheduler's own GL contexts, created on the GL thread's stack in Run()
	/// and destroyed there at shutdown. Never stored on the shared Implementation:
	/// these handles are context-affine and only ever touched by the GL thread.
	struct SchedulerContextHandles {
#if defined(_WIN32)
		HGLRC context = nullptr;
#elif defined(__ANDROID__)
		EGLContext context = EGL_NO_CONTEXT;
#elif defined(__linux__)
		GLXContext glx_context = nullptr;
		EGLContext egl_context = EGL_NO_CONTEXT;
#endif
	};

	/// Creates and binds the scheduler's own context, sharing with the instance's
	/// anchor context (shared_rc). The main render context (instance.rc) is never
	/// bound on the scheduler thread.
	void CreateSchedulerContext(Backend::SchedulerContext::Implementation& scheduler, SchedulerContextHandles& handles) {
		Backend::Instance const& instance = *scheduler.instance;
#if defined(_WIN32)
		handles.context = wglCreateContext(instance.dc);
		if (!handles.context) {
			throw std::runtime_error("OpenGL: wglCreateContext failed for scheduler");
		}
		if (!wglShareLists(instance.shared_rc, handles.context)) {
			wglDeleteContext(handles.context);
			handles.context = nullptr;
			throw std::runtime_error("OpenGL: wglShareLists failed for scheduler context");
		}
		if (!wglMakeCurrent(instance.dc, handles.context)) {
			wglDeleteContext(handles.context);
			handles.context = nullptr;
			throw std::runtime_error("OpenGL: wglMakeCurrent failed for scheduler context");
		}
#elif defined(__ANDROID__)
		auto const& egl = std::get<Backend::Instance::EGL>(instance.gl_handle);
		handles.context = eglCreateContext(egl.display, egl.config, egl.context, nullptr);
		if (handles.context == EGL_NO_CONTEXT) {
			throw std::runtime_error("OpenGL: eglCreateContext failed for scheduler");
		}
		if (!eglMakeCurrent(egl.display, egl.draw, egl.read, handles.context)) {
			eglDestroyContext(egl.display, handles.context);
			handles.context = EGL_NO_CONTEXT;
			throw std::runtime_error("OpenGL: eglMakeCurrent failed for scheduler context");
		}
#elif defined(__linux__)
		if (auto const* glx = std::get_if<Backend::Instance::GLX>(&instance.gl_handle)) {
			constexpr int attributes[] = {
				GLX_CONTEXT_MAJOR_VERSION_ARB, 4,
				GLX_CONTEXT_MINOR_VERSION_ARB, 3,
				None
			};
			handles.glx_context = glXCreateContextAttribsARB(
				glx->dpy,
				glx->fbconfig,
				glx->ctx,
				GL_TRUE,
				attributes
			);
			if (!handles.glx_context) {
				throw std::runtime_error("OpenGL: glXCreateContextAttribsARB failed for scheduler");
			}
			if (!glXMakeCurrent(glx->dpy, glx->drawable, handles.glx_context)) {
				glXDestroyContext(glx->dpy, handles.glx_context);
				handles.glx_context = nullptr;
				throw std::runtime_error("OpenGL: glXMakeCurrent failed for scheduler context");
			}
		}
		else {
			auto const& egl = std::get<Backend::Instance::EGL>(instance.gl_handle);
			handles.egl_context = eglCreateContext(egl.display, egl.config, egl.context, nullptr);
			if (handles.egl_context == EGL_NO_CONTEXT) {
				throw std::runtime_error("OpenGL: eglCreateContext failed for scheduler");
			}
			if (!eglMakeCurrent(egl.display, egl.draw, egl.read, handles.egl_context)) {
				eglDestroyContext(egl.display, handles.egl_context);
				handles.egl_context = EGL_NO_CONTEXT;
				throw std::runtime_error("OpenGL: eglMakeCurrent failed for scheduler context");
			}
		}
#endif
	}

	/// Binds the scheduler context back on the instance's main drawable. This is
	/// the restore point after Present switched the context to a target window.
	void MakeCurrentScheduler(Backend::SchedulerContext::Implementation& scheduler, SchedulerContextHandles const& handles) {
		Backend::Instance const& instance = *scheduler.instance;
#if defined(_WIN32)
		if (!wglMakeCurrent(instance.dc, handles.context)) {
			throw std::runtime_error("OpenGL: wglMakeCurrent failed restoring scheduler context");
		}
#elif defined(__ANDROID__)
		auto const& egl = std::get<Backend::Instance::EGL>(instance.gl_handle);
		if (!eglMakeCurrent(egl.display, egl.draw, egl.read, handles.context)) {
			throw std::runtime_error("OpenGL: eglMakeCurrent failed restoring scheduler context");
		}
#elif defined(__linux__)
		if (auto const* glx = std::get_if<Backend::Instance::GLX>(&instance.gl_handle)) {
			if (!glXMakeCurrent(glx->dpy, glx->drawable, handles.glx_context)) {
				throw std::runtime_error("OpenGL: glXMakeCurrent failed restoring scheduler context");
			}
		}
		else {
			auto const& egl = std::get<Backend::Instance::EGL>(instance.gl_handle);
			if (!eglMakeCurrent(egl.display, egl.draw, egl.read, handles.egl_context)) {
				throw std::runtime_error("OpenGL: eglMakeCurrent failed restoring scheduler context");
			}
		}
#endif
	}

	/// Destroys the scheduler context on the GL thread at shutdown.
	void DestroySchedulerContext(Backend::SchedulerContext::Implementation& scheduler, SchedulerContextHandles& handles) {
#if defined(_WIN32)
		if (handles.context) {
			wglMakeCurrent(nullptr, nullptr);
			wglDeleteContext(handles.context);
			handles.context = nullptr;
		}
#elif defined(__ANDROID__)
		Backend::Instance const& instance = *scheduler.instance;
		auto const& egl = std::get<Backend::Instance::EGL>(instance.gl_handle);
		if (handles.context != EGL_NO_CONTEXT) {
			eglMakeCurrent(egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
			eglDestroyContext(egl.display, handles.context);
			handles.context = EGL_NO_CONTEXT;
		}
#elif defined(__linux__)
		Backend::Instance const& instance = *scheduler.instance;
		if (auto const* glx = std::get_if<Backend::Instance::GLX>(&instance.gl_handle)) {
			if (handles.glx_context) {
				glXMakeCurrent(glx->dpy, None, nullptr);
				glXDestroyContext(glx->dpy, handles.glx_context);
				handles.glx_context = nullptr;
			}
		}
		else {
			auto const& egl = std::get<Backend::Instance::EGL>(instance.gl_handle);
			if (handles.egl_context != EGL_NO_CONTEXT) {
				eglMakeCurrent(egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
				eglDestroyContext(egl.display, handles.egl_context);
				handles.egl_context = EGL_NO_CONTEXT;
			}
		}
#endif
	}

	void SetSwapInterval(Backend::Instance const& instance, bool vertical_sync) {
		(void)instance;
		(void)vertical_sync;
#if defined(_WIN32)
		using SwapIntervalProc = BOOL(APIENTRY*)(int);
		auto swap_interval = reinterpret_cast<SwapIntervalProc>(
			wglGetProcAddress("wglSwapIntervalEXT")
		);
		if (swap_interval) {
			swap_interval(vertical_sync ? 1 : 0);
		}
#elif defined(__ANDROID__) || defined(__linux__)
		auto const* egl = std::get_if<Backend::Instance::EGL>(&instance.gl_handle);
		if (egl) {
			eglSwapInterval(egl->display, vertical_sync ? 1 : 0);
		}
#endif
	}

	using PresentTarget = Backend::SchedulerContext::Implementation::PresentTarget;

	/// Restores the render context's drawable on scope exit. Present switches the
	/// context to a target window's HDC/surface; without this guard an exception
	/// between the switch and the restore would leave the context on the wrong
	/// drawable, corrupting every later submission and glGetError on this thread.
	struct ContextRestore {
		Backend::SchedulerContext::Implementation& scheduler;
		SchedulerContextHandles const& handles;
		~ContextRestore() noexcept {
			try {
				MakeCurrentScheduler(scheduler, handles);
			}
			catch (...) {
				// Context switch failures are surfaced through the token error path.
			}
		}
	};

	/// Finds or creates the presentable drawable for a platform handle. A GL context
	/// can be made current on any drawable sharing its pixel format, so the render
	/// context is reused and each window only needs its own HDC/surface.
	PresentTarget& AcquirePresentTarget(
		Backend::SchedulerContext::Implementation& scheduler,
		Backend::PlatformHandle const& handle
	) {
		for (auto& target : scheduler.present_targets) {
			if (target.handle == handle) {
				return target;
			}
		}
		Backend::Instance const& instance = *scheduler.instance;
		PresentTarget target;
		target.handle = handle;
#if defined(_WIN32)
		target.dc = GetDC(handle);
		if (!target.dc) {
			throw std::runtime_error("OpenGL present: GetDC failed for the target window");
		}
		// Match the render context's pixel format so wglMakeCurrent accepts the HDC.
		int pixel_format = GetPixelFormat(instance.dc);
		PIXELFORMATDESCRIPTOR descriptor{};
		descriptor.nSize = sizeof(descriptor);
		DescribePixelFormat(instance.dc, pixel_format, sizeof(descriptor), &descriptor);
		SetPixelFormat(target.dc, pixel_format, &descriptor);
#elif defined(__ANDROID__)
		auto const& egl = std::get<Backend::Instance::EGL>(instance.gl_handle);
		target.surface = eglCreateWindowSurface(egl.display, egl.config, handle, nullptr);
		if (target.surface == EGL_NO_SURFACE) {
			throw std::runtime_error("OpenGL present: eglCreateWindowSurface failed");
		}
#elif defined(__linux__)
		if (auto const* egl = std::get_if<Backend::Instance::EGL>(&instance.gl_handle)) {
			auto const& wayland = std::get<Backend::WaylandPlatformHandle>(handle);
			target.surface = eglCreateWindowSurface(
				egl->display,
				egl->config,
				reinterpret_cast<EGLNativeWindowType>(wayland.surface),
				nullptr
			);
			if (target.surface == EGL_NO_SURFACE) {
				throw std::runtime_error("OpenGL present: eglCreateWindowSurface failed");
			}
		}
		// GLX presents directly on the X11 window drawable; nothing to create.
#endif
		scheduler.present_targets.emplace_back(std::move(target));
		return scheduler.present_targets.back();
	}

	void MakeCurrentTarget(
		Backend::SchedulerContext::Implementation& scheduler,
		PresentTarget const& target,
		SchedulerContextHandles const& handles
	) {
#if defined(_WIN32)
		if (!wglMakeCurrent(target.dc, handles.context)) {
			throw std::runtime_error("OpenGL present: wglMakeCurrent on target failed");
		}
#elif defined(__ANDROID__)
		Backend::Instance const& instance = *scheduler.instance;
		auto const& egl = std::get<Backend::Instance::EGL>(instance.gl_handle);
		if (!eglMakeCurrent(egl.display, target.surface, target.surface, handles.context)) {
			throw std::runtime_error("OpenGL present: eglMakeCurrent on target failed");
		}
#elif defined(__linux__)
		Backend::Instance const& instance = *scheduler.instance;
		if (auto const* glx = std::get_if<Backend::Instance::GLX>(&instance.gl_handle)) {
			auto const& x11 = std::get<Backend::X11PlatformHandle>(target.handle);
			if (!glXMakeCurrent(glx->dpy, x11.window, handles.glx_context)) {
				throw std::runtime_error("OpenGL present: glXMakeCurrent on target failed");
			}
		}
		else {
			auto const& egl = std::get<Backend::Instance::EGL>(instance.gl_handle);
			if (!eglMakeCurrent(egl.display, target.surface, target.surface, handles.egl_context)) {
				throw std::runtime_error("OpenGL present: eglMakeCurrent on target failed");
			}
		}
#endif
	}

	void SwapTarget(Backend::Instance const& instance, PresentTarget const& target) {
#if defined(_WIN32)
		::SwapBuffers(target.dc);
#elif defined(__ANDROID__)
		auto const& egl = std::get<Backend::Instance::EGL>(instance.gl_handle);
		eglSwapBuffers(egl.display, target.surface);
#elif defined(__linux__)
		if (auto const* glx = std::get_if<Backend::Instance::GLX>(&instance.gl_handle)) {
			auto const& x11 = std::get<Backend::X11PlatformHandle>(target.handle);
			glXSwapBuffers(glx->dpy, x11.window);
		}
		else {
			auto const& egl = std::get<Backend::Instance::EGL>(instance.gl_handle);
			eglSwapBuffers(egl.display, target.surface);
		}
#endif
	}

	std::pair<GLsizei, GLsizei> TargetFramebufferSize(
		Backend::Instance const& instance,
		PresentTarget const& target
	) {
#if defined(_WIN32)
		RECT rect{};
		GetClientRect(target.handle, &rect);
		return { rect.right - rect.left, rect.bottom - rect.top };
#elif defined(__ANDROID__)
		auto const& egl = std::get<Backend::Instance::EGL>(instance.gl_handle);
		EGLint width = 0;
		EGLint height = 0;
		eglQuerySurface(egl.display, target.surface, EGL_WIDTH, &width);
		eglQuerySurface(egl.display, target.surface, EGL_HEIGHT, &height);
		return { width, height };
#elif defined(__linux__)
		if (auto const* glx = std::get_if<Backend::Instance::GLX>(&instance.gl_handle)) {
			auto const& x11 = std::get<Backend::X11PlatformHandle>(target.handle);
			unsigned int width = 0;
			unsigned int height = 0;
			glXQueryDrawable(glx->dpy, x11.window, GLX_WIDTH, &width);
			glXQueryDrawable(glx->dpy, x11.window, GLX_HEIGHT, &height);
			return { static_cast<GLsizei>(width), static_cast<GLsizei>(height) };
		}
		auto const& egl = std::get<Backend::Instance::EGL>(instance.gl_handle);
		EGLint width = 0;
		EGLint height = 0;
		eglQuerySurface(egl.display, target.surface, EGL_WIDTH, &width);
		eglQuerySurface(egl.display, target.surface, EGL_HEIGHT, &height);
		return { width, height };
#endif
	}

} // namespace

namespace fyuu_rhi::opengl {

	using namespace fyuu_rhi::execution;
	using namespace fyuu_rhi::pipeline;
	using Submission = Backend::Submission;

	struct Backend::CompletionToken::Implementation {
		/// Keeps the scheduler's pending mutex alive for the Deleter handshake.
		std::shared_ptr<SchedulerContext::Implementation> scheduler;
		std::atomic<bool> complete = false;
		std::atomic<bool> stopped = false;
		std::exception_ptr error;
	};

	void Backend::CompletionToken::Deleter::operator()(Implementation* impl) const noexcept {
		if (impl && impl->scheduler) {
			// The GL thread stores complete under the same lock: acquiring it here
			// either observes the store or removes this state before the thread can
			// ever touch it, so destruction never races a write.
			std::unique_lock<std::mutex> lock(impl->scheduler->pending_mutex);
			std::erase_if(
				impl->scheduler->pending,
				[impl](SchedulerContext::Implementation::PendingSync const& pending) {
					return pending.state == impl;
				}
			);
		}
		delete impl;
	}

	Backend::CompletionToken::~CompletionToken() noexcept = default;

	bool Backend::CompletionToken::Poll() noexcept {
		return impl && impl->complete.load(std::memory_order_acquire);
	}

	std::exception_ptr Backend::CompletionToken::Error() const noexcept {
		return impl ? impl->error : nullptr;
	}

	bool Backend::CompletionToken::IsStopped() const noexcept {
		return impl && impl->stopped.load(std::memory_order_acquire);
	}

	void Backend::SchedulerContext::Implementation::ReapSignaled() {
		for (;;) {
			PendingSync current;
			{
				std::unique_lock<std::mutex> lock(pending_mutex);
				if (pending.empty()) {
					return;
				}
				// Non-blocking: GL_TIMEOUT_EXPIRED means the fence has not fired yet.
				// FIFO order is preserved by stopping at the first unfinished sync.
				if (glClientWaitSync(pending.front().sync, 0u, 0u) == GL_TIMEOUT_EXPIRED) {
					return;
				}
				current = std::move(pending.front());
				pending.pop_front();
				current.state->complete.store(true, std::memory_order_release);
			}
			glDeleteSync(current.sync);
		}
	}

	void Backend::SchedulerContext::Implementation::DrainPendingOnShutdown() {
		std::unique_lock<std::mutex> lock(pending_mutex);
		for (auto& pending_sync : pending) {
			pending_sync.state->complete.store(true, std::memory_order_release);
			glDeleteSync(pending_sync.sync);
		}
		pending.clear();
	}

	/// Replays one submission against the current GL context. GL is immediate mode,
	/// so "recording" is executing; the front-end plan is the command stream itself.
	struct Replayer {
		Submission const& submission;
		Backend::SchedulerContext::Implementation& scheduler;
		Backend::Instance const& instance;
		SchedulerContextHandles const& handles;

		Submission::PipelineSnapshot const* pipeline = nullptr;
		GLuint vao = 0u;
		GLuint framebuffer = 0u;
		IndexType index_type = IndexType::Uint16;
		bool rendering = false;
		/// Height of the active render pass area. Scissor rectangles are top-left
		/// origin (D3D12/Vulkan convention), while GL scissors are bottom-left
		/// origin, so the Y coordinate must be mirrored against this height.
		std::uint32_t render_area_height = 0u;
		/// Color attachments of the active render pass, for MSAA resolve at EndRendering.
		std::vector<ColorAttachment> active_colors;

		Submission::ResourceSnapshot const& ResourceAt(std::size_t index) const {
			return submission.resources.at(index);
		}

		Submission::ViewSnapshot const& ViewAt(std::size_t index) const {
			return submission.views.at(index);
		}

		/// Binds the pipeline's program, rasterization and depth state, and ensures
		/// a VAO exists for its vertex layout.
		void BindPipelineState(Submission::PipelineSnapshot const& value) {
			glUseProgram(value.impl);
			pipeline = &value;

			if (value.compute) {
				return;
			}

			// Rasterization state.
			if (value.rasterization.cull_mode == CullMode::None) {
				glDisable(GL_CULL_FACE);
			}
			else {
				glEnable(GL_CULL_FACE);
				glCullFace(value.rasterization.cull_mode == CullMode::Front ? GL_FRONT : GL_BACK);
			}
			glFrontFace(
				value.rasterization.front_face == FrontFace::CounterClockwise
					? GL_CCW
					: GL_CW
			);

			// Depth state.
			if (value.depth_stencil && value.depth_stencil->depth_test_enabled) {
				glEnable(GL_DEPTH_TEST);
				glDepthFunc(NativeCompareOp(value.depth_stencil->depth_compare));
			}
			else {
				glDisable(GL_DEPTH_TEST);
			}
			glDepthMask(value.depth_stencil ? (value.depth_stencil->depth_write_enabled ? GL_TRUE : GL_FALSE) : GL_FALSE);

			// Blend state (fixed-function blending).
			if (value.blend) {
				glEnable(GL_BLEND);
				glBlendFuncSeparate(
					NativeBlendFactor(value.blend->color.source_factor),
					NativeBlendFactor(value.blend->color.destination_factor),
					NativeBlendFactor(value.blend->alpha.source_factor),
					NativeBlendFactor(value.blend->alpha.destination_factor)
				);
				glBlendEquationSeparate(
					NativeBlendOperation(value.blend->color.operation),
					NativeBlendOperation(value.blend->alpha.operation)
				);
			}
			else {
				glDisable(GL_BLEND);
			}
			glColorMask(
				(value.write_mask & ColorWriteMask::Red) != ColorWriteMask::None ? GL_TRUE : GL_FALSE,
				(value.write_mask & ColorWriteMask::Green) != ColorWriteMask::None ? GL_TRUE : GL_FALSE,
				(value.write_mask & ColorWriteMask::Blue) != ColorWriteMask::None ? GL_TRUE : GL_FALSE,
				(value.write_mask & ColorWriteMask::Alpha) != ColorWriteMask::None ? GL_TRUE : GL_FALSE
			);

			// Vertex array: attributes are fixed by the pipeline; vertex buffers are
			// rebound through glVertexArrayVertexBuffer on each BindVertexBuffer.
			auto found = scheduler.vertex_arrays.find(value.impl);
			if (found == scheduler.vertex_arrays.end()) {
				GLuint created = 0u;
				glCreateVertexArrays(1u, &created);
				for (auto const& attribute : value.vertex_attributes) {
					GLint size = 0;
					GLenum type = 0u;
					AttributeClass attribute_class = AttributeClass::Float;
					AttributeFormat(attribute.format, size, type, attribute_class);
					glEnableVertexArrayAttrib(created, attribute.location);
					if (attribute_class == AttributeClass::Integer) {
						glVertexArrayAttribIFormat(
							created,
							attribute.location,
							size,
							type,
							static_cast<GLuint>(attribute.offset)
						);
					}
					else {
						glVertexArrayAttribFormat(
							created,
							attribute.location,
							size,
							type,
							attribute_class == AttributeClass::Normalized ? GL_TRUE : GL_FALSE,
							static_cast<GLuint>(attribute.offset)
						);
					}
					glVertexArrayAttribBinding(created, attribute.location, attribute.slot);
				}
				for (auto const& buffer : value.vertex_buffers) {
					glVertexArrayBindingDivisor(
						created,
						buffer.slot,
						buffer.input_rate == VertexInputRate::Instance ? 1u : 0u
					);
				}
				scheduler.vertex_arrays.emplace(value.impl, created);
				found = scheduler.vertex_arrays.find(value.impl);
			}
			vao = found->second;
			glBindVertexArray(vao);
		}

		/// Attaches a texture view to the current framebuffer. Layered targets use
		/// glFramebufferTexture (all layers); 2D targets use glFramebufferTexture2D.
		void Attach(Submission::ViewSnapshot const& view, GLenum attachment_point) {
			if (view.target == GL_TEXTURE_2D ||
				view.target == GL_TEXTURE_2D_MULTISAMPLE ||
				view.target == GL_TEXTURE_RECTANGLE) {
				glFramebufferTexture2D(
					GL_FRAMEBUFFER,
					attachment_point,
					view.target,
					view.impl,
					0
				);
			}
			else {
				glFramebufferTexture(GL_FRAMEBUFFER, attachment_point, view.impl, 0);
			}
		}

		void operator()(BeginRendering const& value) {
			if (rendering) {
				throw std::logic_error("OpenGL BeginRendering cannot be nested");
			}
			if (!value.colors.empty() || value.depth_stencil) {
				glGenFramebuffers(1u, &framebuffer);
				glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
				std::vector<GLenum> draw_buffers;
				draw_buffers.reserve(value.colors.size());
				for (std::size_t index = 0u; index < value.colors.size(); ++index) {
					auto const& attachment = value.colors[index];
					Attach(ViewAt(attachment.view), GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index));
					draw_buffers.emplace_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index));
				}
				if (!draw_buffers.empty()) {
					glDrawBuffers(static_cast<GLsizei>(draw_buffers.size()), draw_buffers.data());
				}
				else {
					// Depth-only rendering: no color writes.
					GLenum none = GL_NONE;
					glDrawBuffers(1u, &none);
				}
			}
			else {
				glBindFramebuffer(GL_FRAMEBUFFER, 0u);
				framebuffer = 0u;
			}
			if (value.depth_stencil) {
				auto const& view = ViewAt(value.depth_stencil->view);
				// A combined depth-stencil image attaches to the combined point;
				// depth-only formats attach to the depth point.
				auto point = HasStencil(view.format)
					? GL_DEPTH_STENCIL_ATTACHMENT
					: GL_DEPTH_ATTACHMENT;
				Attach(view, point);
			}

			glViewport(
				value.area.x,
				value.area.y,
				static_cast<GLsizei>(value.area.width),
				static_cast<GLsizei>(value.area.height)
			);
			glScissor(
				value.area.x,
				value.area.y,
				static_cast<GLsizei>(value.area.width),
				static_cast<GLsizei>(value.area.height)
			);
			// Unlike D3D12/Vulkan, scissor testing is opt-in; clears and draws rely on it.
			glEnable(GL_SCISSOR_TEST);

			// Discard precedes clear: glInvalidateFramebuffer drops the whole buffer,
			// so clearing first would let a discard wipe a fresh clear (D3D12 lesson).
			if (value.colors.empty() == false || value.depth_stencil) {
				std::vector<GLenum> attachments;
				for (std::size_t index = 0u; index < value.colors.size(); ++index) {
					if (value.colors[index].load == LoadOperation::Discard) {
						attachments.emplace_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index));
					}
				}
				if (value.depth_stencil &&
					(value.depth_stencil->depth_load == LoadOperation::Discard ||
						value.depth_stencil->stencil_load == LoadOperation::Discard)) {
					attachments.emplace_back(GL_DEPTH_STENCIL_ATTACHMENT);
				}
				if (!attachments.empty()) {
					glInvalidateFramebuffer(
						GL_FRAMEBUFFER,
						static_cast<GLsizei>(attachments.size()),
						attachments.data()
					);
				}
			}
			// Per-attachment clears via glClearBuffer* (respects the scissor set above).
			for (std::size_t index = 0u; index < value.colors.size(); ++index) {
				auto const& attachment = value.colors[index];
				if (attachment.load == LoadOperation::Clear) {
					GLfloat rgba[4] = {
						attachment.clear.red,
						attachment.clear.green,
						attachment.clear.blue,
						attachment.clear.alpha
					};
					glClearBufferfv(GL_COLOR, static_cast<GLint>(index), rgba);
				}
			}
			if (value.depth_stencil) {
				auto const& attachment = *value.depth_stencil;
				bool depth_clear = attachment.depth_load == LoadOperation::Clear;
				bool stencil_clear = attachment.stencil_load == LoadOperation::Clear;
				if (depth_clear && stencil_clear) {
					glClearBufferfi(
						GL_DEPTH_STENCIL,
						0,
						attachment.clear_depth,
						static_cast<GLint>(attachment.clear_stencil)
					);
				}
				else if (depth_clear) {
					glClearBufferfv(GL_DEPTH, 0, &attachment.clear_depth);
				}
				else if (stencil_clear) {
					glClearBufferiv(
						GL_STENCIL,
						0,
						reinterpret_cast<GLint const*>(&attachment.clear_stencil)
					);
				}
			}
			rendering = true;
			render_area_height = value.area.height;
			active_colors = value.colors;
		}

		void operator()(EndRendering const&) {
			if (!rendering) {
				throw std::logic_error("OpenGL EndRendering without BeginRendering");
			}
			rendering = false;
			// Resolve MSAA color attachments before tearing down the render FBO.
			for (std::size_t index = 0u; index < active_colors.size(); ++index) {
				auto const& attachment = active_colors[index];
				if (!attachment.resolve_resource || !attachment.resolve_view) {
					continue;
				}
				auto const& source = ResourceAt(attachment.resource);
				auto const& resolve_view = ViewAt(*attachment.resolve_view);
				GLuint resolve_framebuffer = 0u;
				glGenFramebuffers(1u, &resolve_framebuffer);
				glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
				glReadBuffer(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(index));
				glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_framebuffer);
				glFramebufferTexture2D(
					GL_DRAW_FRAMEBUFFER,
					GL_COLOR_ATTACHMENT0,
					resolve_view.target,
					resolve_view.impl,
					0
				);
				// glBlitFramebuffer is one of the few fragment operations that is
				// still affected by the scissor test, so a resolve must not inherit
				// the render pass's scissor rectangle.
				glDisable(GL_SCISSOR_TEST);
				glBlitFramebuffer(
					0, 0,
					static_cast<GLint>(source.width), static_cast<GLint>(source.height),
					0, 0,
					static_cast<GLint>(source.width), static_cast<GLint>(source.height),
					GL_COLOR_BUFFER_BIT,
					GL_NEAREST
				);
				glDeleteFramebuffers(1u, &resolve_framebuffer);
			}
			active_colors.clear();
			glBindFramebuffer(GL_FRAMEBUFFER, 0u);
			if (framebuffer != 0u) {
				glDeleteFramebuffers(1u, &framebuffer);
				framebuffer = 0u;
			}
			vao = 0u;
		}

		void operator()(BindPipeline const& value) {
			BindPipelineState(submission.pipelines.at(value.pipeline));
		}

		void operator()(BindResourceGroup const& value) {
			if (!pipeline) {
				throw std::logic_error("OpenGL resource group requires a bound pipeline");
			}
			// Contract: slang emits GLSL with layout(binding = slot + array_element),
			// matching the binding units/uniform/texture/image indices used here.
			auto const& group = submission.groups.at(value.group);
			for (auto const& binding : group.bindings) {
				GLenum unit = binding.slot + binding.array_element;
				if (binding.buffer != 0u) {
					auto target = GL_UNIFORM_BUFFER;
					for (auto const& metadata : pipeline->bindings) {
						if (metadata.slot == binding.slot) {
							target = BufferTarget(metadata.flags);
							break;
						}
					}
					glBindBufferBase(target, unit, binding.buffer);
					continue;
				}
				if (binding.view == 0u) {
					if (binding.sampler != 0u) {
						glBindSampler(unit, binding.sampler);
					}
					continue;
				}
				// Distinguish a storage image from a sampled texture through the
				// pipeline's reflected binding flags for this slot.
				bool storage = false;
				for (auto const& metadata : pipeline->bindings) {
					if (metadata.slot == binding.slot) {
						storage = metadata.flags.Test(ResourceFlagBits::StorageBinding);
						break;
					}
				}
				if (storage) {
					glBindImageTexture(
						unit,
						binding.view,
						0,
						GL_TRUE,
						0,
						GL_READ_WRITE,
						binding.view_format
					);
				}
				else {
					glActiveTexture(GL_TEXTURE0 + unit);
					glBindTexture(binding.view_target, binding.view);
				}
				if (binding.sampler != 0u) {
					glBindSampler(unit, binding.sampler);
				}
			}
		}

		void operator()(BindVertexBuffer const& value) {
			if (!pipeline || vao == 0u) {
				throw std::logic_error("OpenGL vertex buffer requires a bound graphics pipeline");
			}
			auto const& resource = ResourceAt(value.resource);
			glVertexArrayVertexBuffer(
				vao,
				value.slot,
				resource.impl,
				static_cast<GLintptr>(value.offset),
				static_cast<GLsizei>(value.stride)
			);
		}

		void operator()(BindIndexBuffer const& value) {
			if (!pipeline || vao == 0u) {
				throw std::logic_error("OpenGL index buffer requires a bound graphics pipeline");
			}
			auto const& resource = ResourceAt(value.resource);
			glVertexArrayElementBuffer(vao, resource.impl);
			index_type = value.type;
		}

		void operator()(Viewport const& value) {
			// Viewport rectangles are top-left origin (D3D12/Vulkan/WebGPU
			// convention), but GL viewports are bottom-left origin: mirror Y
			// within the render area. A full-window viewport is unaffected.
			std::int32_t y = static_cast<std::int32_t>(value.y);
			if (rendering) {
				y = static_cast<std::int32_t>(render_area_height) -
					static_cast<std::int32_t>(value.y + value.height);
			}
			glViewport(
				static_cast<GLint>(value.x),
				y,
				static_cast<GLsizei>(value.width),
				static_cast<GLsizei>(value.height)
			);
		}

		void operator()(Scissor const& value) {
			// Scissor rectangles are top-left origin, but GL scissors are
			// bottom-left origin: mirror Y within the render area so clipping
			// follows the drawn (Y-flipped) geometry instead of its mirror image.
			std::int32_t y = value.y;
			if (rendering) {
				y = static_cast<std::int32_t>(render_area_height) -
					value.y - static_cast<std::int32_t>(value.height);
			}
			glScissor(
				value.x,
				y,
				static_cast<GLsizei>(value.width),
				static_cast<GLsizei>(value.height)
			);
			glEnable(GL_SCISSOR_TEST);
		}

		void operator()(Draw const& value) {
			if (!pipeline) {
				throw std::logic_error("OpenGL draw requires a bound pipeline");
			}
			glDrawArrays(
				PrimitiveMode(pipeline->primitive.topology),
				static_cast<GLint>(value.first_vertex),
				static_cast<GLsizei>(value.vertex_count)
			);
		}

		void operator()(DrawIndexed const& value) {
			if (!pipeline) {
				throw std::logic_error("OpenGL indexed draw requires a bound pipeline");
			}
			// The index type is carried by the preceding BindIndexBuffer command.
			auto native_index_type = index_type == IndexType::Uint16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
			auto native_index_size = index_type == IndexType::Uint16 ? 2u : 4u;
			glDrawElementsInstancedBaseVertexBaseInstance(
				PrimitiveMode(pipeline->primitive.topology),
				static_cast<GLsizei>(value.index_count),
				native_index_type,
				reinterpret_cast<void const*>(static_cast<std::uintptr_t>(value.first_index * native_index_size)),
				static_cast<GLsizei>(value.instance_count),
				static_cast<GLint>(value.vertex_offset),
				static_cast<GLuint>(value.first_instance)
			);
		}

		void operator()(Dispatch const& value) {
			glDispatchCompute(
				value.group_count_x,
				value.group_count_y,
				value.group_count_z
			);
		}

		void operator()(CopyBuffer const& value) {
			auto const& source = ResourceAt(value.source);
			auto const& destination = ResourceAt(value.destination);
			// glCopyNamedBufferSubData (GL 4.5 DSA) takes object names directly;
			// glCopyBufferSubData instead takes buffer targets, not names.
			glCopyNamedBufferSubData(
				source.impl,
				destination.impl,
				static_cast<GLintptr>(value.source_offset),
				static_cast<GLintptr>(value.destination_offset),
				static_cast<GLsizeiptr>(value.size)
			);
		}
		void operator()(WriteBuffer const& value) {
			auto const& resource = ResourceAt(value.resource);
			if (resource.type != Submission::ResourceSnapshot::Type::Buffer) {
				throw std::invalid_argument("OpenGL write requires a buffer resource");
			}
			if (value.offset > resource.size ||
				value.data.size() > resource.size - value.offset) {
				throw std::out_of_range("OpenGL write exceeds the buffer size");
			}
			// Cross-check the actual allocation: the snapshot size must match the
			// real GL buffer, otherwise glNamedBufferSubData overflows.
			GLint64 actual_size = 0;
			glGetNamedBufferParameteri64v(resource.impl, GL_BUFFER_SIZE, &actual_size);
			if (value.offset + value.data.size() > static_cast<std::size_t>(actual_size)) {
				throw std::runtime_error(
					std::format(
						"OpenGL write of {} bytes at offset {} exceeds actual buffer size {} (reported {})",
						value.data.size(), value.offset, actual_size, resource.size
					)
				);
			}
			glNamedBufferSubData(
				resource.impl,
				static_cast<GLintptr>(value.offset),
				static_cast<GLsizeiptr>(value.data.size()),
				value.data.data()
			);
		}

		void operator()(CopyBufferToTexture const& value) {
			auto const& source = ResourceAt(value.source);
			auto const& destination = ResourceAt(value.destination);
			auto const& region = value.destination_region;
			bool is_3d = destination.target == GL_TEXTURE_3D;
			auto z_offset = static_cast<GLint>(is_3d ? region.offset_z : region.base_array_layer);
			auto depth = static_cast<GLsizei>(is_3d ? region.depth : region.array_layer_count);

			GLsizei block_bytes = CompressedBlockBytes(destination.format);
			// glTextureSubImage3D / glCompressedTexSubImage3D are only valid for 3D,
			// 2D-array and cube textures; a plain 2D texture must use the 2D variant.
			bool is_2d = destination.target == GL_TEXTURE_2D;
			if (block_bytes != 0) {
				// Compressed path: 4x4 blocks, tightly packed source required.
				GLsizei blocks_wide = static_cast<GLsizei>((region.width + 3u) / 4u);
				GLsizei blocks_high = static_cast<GLsizei>((region.height + 3u) / 4u);
				auto tight_pitch = static_cast<std::size_t>(blocks_wide) * block_bytes;
				if (value.source_layout.bytes_per_row != 0u &&
					value.source_layout.bytes_per_row != tight_pitch) {
					throw std::invalid_argument(
						"OpenGL compressed upload requires a tightly packed source"
					);
				}
				auto image_size = static_cast<GLsizei>(
					tight_pitch * blocks_high * static_cast<std::size_t>(depth)
				);
				auto source_offset = reinterpret_cast<void const*>(
					static_cast<std::uintptr_t>(value.source_layout.offset)
				);
				glBindBuffer(GL_PIXEL_UNPACK_BUFFER, source.impl);
				if (is_2d) {
					glCompressedTexSubImage2D(
						destination.impl,
						static_cast<GLint>(region.mip_level),
						static_cast<GLint>(region.offset_x),
						static_cast<GLint>(region.offset_y),
						static_cast<GLsizei>(region.width),
						static_cast<GLsizei>(region.height),
						destination.format,
						image_size,
						source_offset
					);
				}
				else {
					glCompressedTexSubImage3D(
						destination.impl,
						static_cast<GLint>(region.mip_level),
						static_cast<GLint>(region.offset_x),
						static_cast<GLint>(region.offset_y),
						z_offset,
						static_cast<GLsizei>(region.width),
						static_cast<GLsizei>(region.height),
						depth,
						destination.format,
						image_size,
						source_offset
					);
				}
				glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0u);
				return;
			}

			GLenum format;
			GLenum type;
			if (!ExternalFormat(destination.format, format, type)) {
				throw std::invalid_argument("OpenGL buffer-to-texture copy: unsupported texture format");
			}
			GLsizei texel = TexelBytes(destination.format);
			if (texel == 0 || value.source_layout.bytes_per_row % static_cast<std::size_t>(texel) != 0u) {
				throw std::invalid_argument("OpenGL buffer-to-texture copy: row pitch is not texel aligned");
			}
			// Pixel-transfer copies respect the source row pitch via UNPACK state.
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, source.impl);
			if (value.source_layout.bytes_per_row != 0u) {
				glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(value.source_layout.bytes_per_row / texel));
			}
			if (value.source_layout.rows_per_image != 0u) {
				glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, static_cast<GLint>(value.source_layout.rows_per_image));
			}
			auto source_offset = reinterpret_cast<void const*>(
				static_cast<std::uintptr_t>(value.source_layout.offset)
			);
			if (is_2d) {
				glTextureSubImage2D(
					destination.impl,
					static_cast<GLint>(region.mip_level),
					static_cast<GLint>(region.offset_x),
					static_cast<GLint>(region.offset_y),
					static_cast<GLsizei>(region.width),
					static_cast<GLsizei>(region.height),
					format,
					type,
					source_offset
				);
			}
			else {
				glTextureSubImage3D(
					destination.impl,
					static_cast<GLint>(region.mip_level),
					static_cast<GLint>(region.offset_x),
					static_cast<GLint>(region.offset_y),
					z_offset,
					static_cast<GLsizei>(region.width),
					static_cast<GLsizei>(region.height),
					depth,
					format,
					type,
					source_offset
				);
			}
			glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
			glPixelStorei(GL_UNPACK_IMAGE_HEIGHT, 0);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0u);
		}

		void operator()(CopyTextureToBuffer const& value) {
			auto const& source = ResourceAt(value.source);
			auto const& destination = ResourceAt(value.destination);
			auto const& region = value.source_region;
			bool is_3d = source.target == GL_TEXTURE_3D;
			auto z_offset = static_cast<GLint>(is_3d ? region.offset_z : region.base_array_layer);
			auto depth = static_cast<GLsizei>(is_3d ? region.depth : region.array_layer_count);

			GLsizei block_bytes = CompressedBlockBytes(source.format);
			if (block_bytes != 0) {
				// Compressed readback: tightly packed destination required.
				GLsizei blocks_wide = static_cast<GLsizei>((region.width + 3u) / 4u);
				auto tight_pitch = static_cast<std::size_t>(blocks_wide) * block_bytes;
				if (value.destination_layout.bytes_per_row != 0u &&
					value.destination_layout.bytes_per_row != tight_pitch) {
					throw std::invalid_argument(
						"OpenGL compressed readback requires a tightly packed destination"
					);
				}
				glBindBuffer(GL_PIXEL_PACK_BUFFER, destination.impl);
				glGetCompressedTextureSubImage(
					source.impl,
					static_cast<GLint>(region.mip_level),
					static_cast<GLint>(region.offset_x),
					static_cast<GLint>(region.offset_y),
					z_offset,
					static_cast<GLsizei>(region.width),
					static_cast<GLsizei>(region.height),
					depth,
					static_cast<GLsizei>(destination.size - value.destination_layout.offset),
					reinterpret_cast<void*>(static_cast<std::uintptr_t>(value.destination_layout.offset))
				);
				glBindBuffer(GL_PIXEL_PACK_BUFFER, 0u);
				return;
			}

			GLenum format;
			GLenum type;
			if (!ExternalFormat(source.format, format, type)) {
				throw std::invalid_argument("OpenGL texture-to-buffer copy: unsupported texture format");
			}
			GLsizei texel = TexelBytes(source.format);
			if (texel == 0 || value.destination_layout.bytes_per_row % static_cast<std::size_t>(texel) != 0u) {
				throw std::invalid_argument("OpenGL texture-to-buffer copy: row pitch is not texel aligned");
			}
			glBindBuffer(GL_PIXEL_PACK_BUFFER, destination.impl);
			if (value.destination_layout.bytes_per_row != 0u) {
				glPixelStorei(GL_PACK_ROW_LENGTH, static_cast<GLint>(value.destination_layout.bytes_per_row / texel));
			}
			if (value.destination_layout.rows_per_image != 0u) {
				glPixelStorei(GL_PACK_IMAGE_HEIGHT, static_cast<GLint>(value.destination_layout.rows_per_image));
			}
			glGetTextureSubImage(
				source.impl,
				static_cast<GLint>(region.mip_level),
				static_cast<GLint>(region.offset_x),
				static_cast<GLint>(region.offset_y),
				static_cast<GLint>(is_3d ? region.offset_z : region.base_array_layer),
				static_cast<GLsizei>(region.width),
				static_cast<GLsizei>(region.height),
				static_cast<GLsizei>(is_3d ? region.depth : region.array_layer_count),
				format,
				type,
				static_cast<GLsizei>(destination.size - value.destination_layout.offset),
				reinterpret_cast<void*>(static_cast<std::uintptr_t>(value.destination_layout.offset))
			);
			glPixelStorei(GL_PACK_ROW_LENGTH, 0);
			glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0);
			glBindBuffer(GL_PIXEL_PACK_BUFFER, 0u);
		}

		void operator()(CopyTexture const& value) {
			auto const& source = ResourceAt(value.source);
			auto const& destination = ResourceAt(value.destination);
			auto const& source_region = value.source_region;
			bool source_3d = source.target == GL_TEXTURE_3D;
			bool dest_3d = destination.target == GL_TEXTURE_3D;
			glCopyImageSubData(
				source.impl, source.target, static_cast<GLint>(source_region.mip_level),
				static_cast<GLint>(source_region.offset_x), static_cast<GLint>(source_region.offset_y),
				static_cast<GLint>(source_3d ? source_region.offset_z : source_region.base_array_layer),
				destination.impl, destination.target, static_cast<GLint>(value.destination_region.mip_level),
				static_cast<GLint>(value.destination_region.offset_x),
				static_cast<GLint>(value.destination_region.offset_y),
				static_cast<GLint>(dest_3d ? value.destination_region.offset_z : value.destination_region.base_array_layer),
				static_cast<GLsizei>(source_region.width), static_cast<GLsizei>(source_region.height),
				static_cast<GLsizei>(source_3d ? source_region.depth : source_region.array_layer_count)
			);
		}

		void operator()(Present const& value) {
			auto const& source = ResourceAt(value.source);
			if (value.target >= submission.presentation_targets.size()) {
				throw std::invalid_argument("OpenGL present target index is out of range");
			}
			auto& target = AcquirePresentTarget(scheduler, submission.presentation_targets[value.target]);
			// Present on the target window's own drawable, then restore the render
			// context. FBOs/textures stay valid across drawables of one context.
			// The guard restores the render drawable even if blit/swap throws.
			MakeCurrentTarget(scheduler, target, handles);
			ContextRestore restore{ scheduler, handles };
			auto [width, height] = TargetFramebufferSize(instance, target);
			GLuint read_framebuffer = 0u;
			glGenFramebuffers(1u, &read_framebuffer);
			glBindFramebuffer(GL_READ_FRAMEBUFFER, read_framebuffer);
			glFramebufferTexture2D(
				GL_READ_FRAMEBUFFER,
				GL_COLOR_ATTACHMENT0,
				source.target,
				source.impl,
				0
			);
			glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0u);
			// The scissor test clips the destination of glBlitFramebuffer. The
			// render pass left scissor testing enabled with the render target's
			// rectangle (or an ImGui clip rect) as the last value, so presenting
			// through it would leave the rest of the window showing stale back
			// buffer content - flashing and broken output on resize. Disable it
			// so the full window is refreshed.
			glDisable(GL_SCISSOR_TEST);
			glBlitFramebuffer(
				0, 0,
				static_cast<GLint>(source.width), static_cast<GLint>(source.height),
				0, 0,
				width, height,
				GL_COLOR_BUFFER_BIT,
				GL_LINEAR
			);
			glDeleteFramebuffers(1u, &read_framebuffer);
			glBindFramebuffer(GL_FRAMEBUFFER, 0u);
			SetSwapInterval(instance, value.vertical_sync);
			// Finish the blit before presenting: the blit runs on this window context
			// and SwapBuffers presents whatever is in the back buffer, so swapping an
			// incomplete blit would flash partial frames. GL also does not order
			// commands across contexts in a share group, so this stall both fixes the
			// present and keeps the next frame's clear from racing the texture read.
			glFinish();
			SwapTarget(instance, target);
			// ContextRestore re-establishes the render drawable on scope exit.
		}
	};

	void Backend::SchedulerContext::Implementation::Run(std::stop_token stop) {
		std::deque<Submission> local_submissions;
		std::mutex local_mutex;
		std::condition_variable local_condition;
		mutex.store(&local_mutex, std::memory_order_relaxed);
		condition.store(&local_condition, std::memory_order_relaxed);
		submissions.store(&local_submissions, std::memory_order_release);
		mutex.notify_all();

		// The GL context lives on this thread's stack: it is created here, bound
		// and used only by the GL thread, and destroyed here at shutdown. Storing
		// it on the shared Implementation would invite cross-thread access.
		SchedulerContextHandles handles;
		// Each scheduler owns its own context sharing with the instance's anchor.
		// The main render context is never bound on this thread. Failure must not
		// terminate the thread: capture it and let ExecuteCommands surface it.
		try {
			CreateSchedulerContext(*this, handles);
		}
		catch (...) {
			fatal_error = std::current_exception();
			DrainPendingOnShutdown();
			submissions.store(nullptr, std::memory_order_release);
			condition.store(nullptr, std::memory_order_release);
			mutex.store(nullptr, std::memory_order_release);
			// Wake CreateScheduler's wait: "ready" means the context attempt finished,
			// success or failure (the failure is reported through fatal_error). A
			// plain store does not wake atomic::wait; notify is required.
			context_ready.store(true, std::memory_order_release);
			context_ready.notify_all();
			return;
		}
		context_ready.store(true, std::memory_order_release);
		context_ready.notify_all();

		for (;;) {
			ReapSignaled();
			Submission current;
			{
				std::unique_lock<std::mutex> lock(local_mutex);
				// Timed wait: pending fences must be re-polled even when no new
				// submission arrives. An indefinite wait would strand the last
				// submission's sync and the token would never complete.
				local_condition.wait_for(lock, std::chrono::milliseconds(1), [&] {
					return !local_submissions.empty() || stop.stop_requested();
				});
				if (stop.stop_requested()) {
					break;
				}
				if (local_submissions.empty()) {
					continue;   // timed out; loop back to ReapSignaled to poll pending
				}
				current = std::move(local_submissions.front());
				local_submissions.pop_front();
			}

			try {
				// Host-side object creation (buffers, shaders, textures) is not
				// error-checked in release builds; drain any error it left on the
				// shared context so replay errors are attributed to replay commands.
				while (glGetError() != GL_NO_ERROR) {}
				Replayer replayer{ current, *this, *instance, handles };
				for (auto const& batch : current.plan.batches) {
					for (auto const& node : batch.nodes) {
						// GL executes in order on one context, so only memory
						// visibility needs a barrier. Emit one merged barrier for
						// writes that this node's reads depend on.
						GLbitfield barrier_mask = 0u;
						for (auto const& barrier : batch.barriers) {
							if (barrier.destination_node == node.id) {
								barrier_mask |= BarrierMask(barrier.source_usage);
							}
						}
						for (auto const& barrier : batch.release_barriers) {
							if (barrier.destination_node == node.id) {
								barrier_mask |= BarrierMask(barrier.source_usage);
							}
						}
						if (barrier_mask != 0u) {
							glMemoryBarrier(barrier_mask);
						}
						for (auto const& command : node.commands) {
							std::visit(replayer, command);
							// Report the offending command so GL errors are debuggable.
							if (GLenum command_error = glGetError(); command_error != GL_NO_ERROR) {
								throw std::runtime_error(
									std::format("OpenGL error 0x{:X} after command {}", command_error, CommandName(command))
								);
							}
						}
					}
				}
				// GL errors are drained per command above, which also names the culprit.
			}
			catch (...) {
				if (current.token_state) {
					current.token_state->error = std::current_exception();
					std::string message = "unknown";
					try {
						std::rethrow_exception(current.token_state->error);
					}
					catch (std::exception const& e) {
						message = e.what();
					}
					catch (...) {
					}
					LOG_WARNING(std::format("OpenGL replay recorded an error: {}", message));
				}
			}
			// Re-assert the scheduler context after every submission: Present switches
			// the drawable, and any partial failure must not leak that state into
			// the next submission or the glGetError drain above.
			MakeCurrentScheduler(*this, handles);
			if (current.token_state) {
				GLsync sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0u);
				{
					std::unique_lock<std::mutex> lock(pending_mutex);
					pending.push_back({ sync, current.token_state });
				}
			}
		}

		DrainPendingOnShutdown();
		DestroySchedulerContext(*this, handles);
		submissions.store(nullptr, std::memory_order_release);
		condition.store(nullptr, std::memory_order_release);
		mutex.store(nullptr, std::memory_order_release);
	}

	Backend::SchedulerContext Backend::CreateScheduler(LogicalDevice const& ld) {
		if (!ld) {
			throw std::invalid_argument("OpenGL scheduler requires a logical device");
		}
		SchedulerContext result;
		result.impl = std::make_shared<SchedulerContext::Implementation>();
		result.impl->instance = ld.instance;
		result.impl->thread = std::jthread(
			[impl = result.impl.get()](std::stop_token stop) {
				impl->Run(std::move(stop));
			}
		);
		// context_ready is stored (release) strictly after the thread-stack queue is
		// published, so acquiring it also makes those pointers visible. Waiting on it
		// alone (not mutex) covers both queue publication and context setup outcome.
		result.impl->context_ready.wait(false, std::memory_order_acquire);
		if (result.impl->fatal_error) {
			std::rethrow_exception(result.impl->fatal_error);
		}
		return result;
	}

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
			throw std::invalid_argument("OpenGL scheduler is not initialized");
		}
		// Phase 1a: validate all caller-owned data before observing stop.
		if (resources.size() != plan.bindings.resource_count ||
			views.size() != plan.bindings.view_count ||
			samplers.size() != plan.bindings.sampler_count ||
			pipelines.size() != plan.bindings.pipeline_count ||
			resource_groups.size() != plan.bindings.resource_group_count) {
			throw std::invalid_argument("OpenGL execution binding count mismatch");
		}
		for (std::size_t index = 0u; index < plan.batches.size(); ++index) {
			if (plan.batches[index].id != index) {
				throw std::invalid_argument("OpenGL execution batch IDs must match storage indices");
			}
			for (auto dependency : plan.batches[index].dependencies) {
				if (dependency >= index) {
					throw std::invalid_argument("OpenGL execution batches are not topologically ordered");
				}
			}
		}
		// A presentation target can be presented at most once per graph, matching
		// the D3D12/Vulkan backends. Without this check duplicate presents would
		// each acquire/swap independently during replay.
		std::vector<PlatformHandle> active_presentation_targets;
		for (auto const& batch : plan.batches) {
			for (auto const& node : batch.nodes) {
				for (auto const& command : node.commands) {
					auto present = std::get_if<Present>(&command);
					if (!present) {
						continue;
					}
					if (present->target >= presentation_targets.size()) {
						throw std::invalid_argument("OpenGL presentation binding is invalid");
					}
					auto target = presentation_targets[present->target];
					if (std::ranges::find(active_presentation_targets, target) !=
						active_presentation_targets.end()) {
						throw std::invalid_argument(
							"OpenGL execution cannot present one target more than once"
						);
					}
					active_presentation_targets.emplace_back(target);
				}
			}
		}

		// The token's member is unique_ptr<Implementation, Deleter>; construct with
		// the matching deleter type (make_unique cannot express it).
		auto token_state = std::unique_ptr<CompletionToken::Implementation, CompletionToken::Deleter>(
			new CompletionToken::Implementation()
		);
		token_state->scheduler = scheduler.impl;
		auto* token_pointer = token_state.get();

		Submission submission;
		submission.plan = plan;
		submission.presentation_targets.assign(
			presentation_targets.begin(),
			presentation_targets.end()
		);
		submission.token_state = token_pointer;

		submission.resources.reserve(resources.size());
		for (auto const& resource : resources) {
			// The GL resource is a plain struct carrying its own type tag.
			auto const& native = resource.get();
			if (native.type == Backend::Resource::Type::Buffer) {
				submission.resources.push_back({
					.impl = native.impl,
					.target = native.target,
					.format = native.format,
					.size = native.size,
					.width = 0u,
					.height = 0u,
					.type = Submission::ResourceSnapshot::Type::Buffer
				});
			}
			else {
				submission.resources.push_back({
					.impl = native.impl,
					.target = native.target,
					.format = native.format,
					.size = 0u,
					.width = native.width,
					.height = native.height,
					.type = Submission::ResourceSnapshot::Type::Texture
				});
			}
		}
		submission.views.reserve(views.size());
		for (auto const& view : views) {
			// Backend::View is itself the variant.
			if (auto texture_view = std::get_if<Backend::GLTextureView>(&view.get())) {
				submission.views.push_back({
					.impl = texture_view->impl,
					.target = texture_view->target,
					.format = texture_view->format,
					.texture = true
				});
			}
			else if (auto buffer_view = std::get_if<Backend::GLBufferView>(&view.get())) {
				submission.views.push_back({
					.impl = buffer_view->impl,
					.texture = false
				});
			}
		}
		submission.samplers.reserve(samplers.size());
		for (auto const& sampler : samplers) {
			submission.samplers.push_back({ .impl = sampler.get().impl });
		}
		submission.pipelines.reserve(pipelines.size());
		for (auto const& pipeline : pipelines) {
			submission.pipelines.push_back({
				.impl = pipeline.get().impl,
				.compute = pipeline.get().compute,
				.vertex_buffers = pipeline.get().vertex_buffers,
				.vertex_attributes = pipeline.get().vertex_attributes,
				.primitive = pipeline.get().primitive,
				.rasterization = pipeline.get().rasterization,
				.depth_stencil = pipeline.get().depth_stencil,
				.blend = pipeline.get().color_targets.empty()
					? std::optional<BlendState>{}
					: pipeline.get().color_targets[0].blend,
				.write_mask = pipeline.get().color_targets.empty()
					? ColorWriteMask::All
					: pipeline.get().color_targets[0].write_mask,
				.bindings = pipeline.get().bindings
			});
		}
		submission.groups.reserve(resource_groups.size());
		for (auto const& group : resource_groups) {
			Submission::GroupSnapshot snapshot;
			snapshot.bindings.reserve(group.get().bindings.size());
			for (auto const& binding : group.get().bindings) {
				Submission::GroupBindingSnapshot entry;
				entry.slot = binding.slot;
				entry.array_element = binding.array_element;
				std::visit(
					[&](auto const& value) {
						using Value = std::remove_cvref_t<decltype(value)>;
						if constexpr (std::same_as<Value, std::monostate>) {
							return;
						}
						else if constexpr (std::same_as<Value, NativePipelineBufferBinding<Backend>>) {
							auto const& resource = value.impl.get();
							if (resource.type == Backend::Resource::Type::Buffer) {
								entry.buffer = resource.impl;
							}
						}
						else if constexpr (std::same_as<Value, NativePipelineViewBinding<Backend>>) {
							// Backend::View is itself the variant.
							if (auto texture_native = std::get_if<Backend::GLTextureView>(&value.get())) {
								entry.view = texture_native->impl;
								entry.view_target = texture_native->target;
								entry.view_format = texture_native->format;
							}
							else if (auto buffer_native = std::get_if<Backend::GLBufferView>(&value.get())) {
								entry.view = buffer_native->impl;
								entry.view_target = GL_TEXTURE_BUFFER;
							}
						}
						else if constexpr (std::same_as<Value, NativePipelineSamplerBinding<Backend>>) {
							entry.sampler = value.get().impl;
						}
						else if constexpr (std::same_as<Value, NativePipelineCombinedBinding<Backend>>) {
							if (auto native = std::get_if<Backend::GLTextureView>(&value.view.get())) {
								entry.view = native->impl;
								entry.view_target = native->target;
								entry.view_format = native->format;
							}
							entry.sampler = value.sampler.get().impl;
						}
					},
					binding.value
				);
				snapshot.bindings.emplace_back(entry);
			}
			submission.groups.emplace_back(std::move(snapshot));
		}

		if (stop_token.stop_requested()) {
			token_state->stopped.store(true, std::memory_order_release);
			token_state->complete.store(true, std::memory_order_release);
			return CompletionToken(std::move(token_state));
		}

		// The render context must have bound successfully or the thread is dead.
		if (!scheduler.impl->context_ready.load(std::memory_order_acquire)) {
			if (scheduler.impl->fatal_error) {
				std::rethrow_exception(scheduler.impl->fatal_error);
			}
			throw std::runtime_error("OpenGL scheduler context is not ready");
		}

		// Post to the GL thread's stack-published queue. CreateScheduler guarantees
		// these pointers are non-null while the scheduler is alive.
		auto* queue = scheduler.impl->submissions.load(std::memory_order_acquire);
		auto* queue_mutex = scheduler.impl->mutex.load(std::memory_order_acquire);
		auto* queue_condition = scheduler.impl->condition.load(std::memory_order_acquire);
		{
			std::unique_lock<std::mutex> lock(*queue_mutex);
			queue->emplace_back(std::move(submission));
		}
		queue_condition->notify_one();
		return CompletionToken(std::move(token_state));
	}

} // namespace fyuu_rhi::opengl
#endif // !defined(__APPLE__)
