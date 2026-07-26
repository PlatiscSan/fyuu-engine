module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <type_traits>
#include <sstream>
#include <system_error>
#include <variant>
#include <source_location>
#endif // !defined(__cpp_lib_modules)
#include <boost/scope/defer.hpp>
#if !defined(__APPLE__)
#include "glad/glad.h"

#if defined(_WIN32)
#include "glad/glad_wgl.h"

#elif defined(__linux__)
#include "glad/glad_glx.h"
#include "glad/glad_egl.h"

#elif defined(__ANDROID__)
#include "glad/glad_egl.h"
#include <android/native_window.h>
#include <android/android_native_app_glue.h>

#endif // defined(_WIN32)
#endif // !defined(__APPLE__)
module fyuu_rhi:opengl_instance;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :opengl_traits;
import :log;
import :cache_system;

namespace {

	using namespace fyuu_rhi;

#if defined(_WIN32)
	thread_local HGLRC s_thread_context = nullptr;
	thread_local HDC s_thread_device_context = nullptr;
	thread_local HWND s_thread_window = nullptr;

	void CleanupSharedContext() {
		if (s_thread_context) {
			wglMakeCurrent(nullptr, nullptr);
			wglDeleteContext(s_thread_context);
			s_thread_context = nullptr;
		}
		if (s_thread_window && s_thread_device_context) {
			ReleaseDC(s_thread_window, s_thread_device_context);
			s_thread_device_context = nullptr;
		}
		if (s_thread_window) {
			DestroyWindow(s_thread_window);
			s_thread_window = nullptr;
		}
	}

	HDC CreateThreadDeviceContext(HDC source) {
		s_thread_window = CreateWindowExW(
			0u,
			L"STATIC",
			L"FyuuEngine OpenGL Worker",
			WS_POPUP,
			0,
			0,
			1,
			1,
			nullptr,
			nullptr,
			GetModuleHandleW(nullptr),
			nullptr
		);
		if (!s_thread_window) {
			throw std::system_error(
				GetLastError(),
				std::system_category(),
				"Failed to create the OpenGL worker window"
			);
		}
		auto device_context = GetDC(s_thread_window);
		if (!device_context) {
			auto error = GetLastError();
			DestroyWindow(s_thread_window);
			s_thread_window = nullptr;
			throw std::system_error(
				error,
				std::system_category(),
				"Failed to get the OpenGL worker device context"
			);
		}
		auto pixel_format = GetPixelFormat(source);
		PIXELFORMATDESCRIPTOR descriptor;
		if (pixel_format == 0 || !DescribePixelFormat(
			source,
			pixel_format,
			sizeof(descriptor),
			&descriptor
		) || !SetPixelFormat(device_context, pixel_format, &descriptor)) {
			auto error = GetLastError();
			ReleaseDC(s_thread_window, device_context);
			DestroyWindow(s_thread_window);
			s_thread_window = nullptr;
			throw std::system_error(
				error,
				std::system_category(),
				"Failed to configure the OpenGL worker pixel format"
			);
		}
		return device_context;
	}
#endif // defined(_WIN32)

	void GLDebugMessager(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, GLchar const* message, void const* user_param) {
		std::ostringstream msg;
		msg << "[OpenGL] ";

		switch (source) {
		case GL_DEBUG_SOURCE_API:				msg << "[API] "; break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM:		msg << "[Window System] "; break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER:	msg << "[Shader Compiler] "; break;
		case GL_DEBUG_SOURCE_THIRD_PARTY:		msg << "[Third Party] "; break;
		case GL_DEBUG_SOURCE_APPLICATION:		msg << "[Application] "; break;
		case GL_DEBUG_SOURCE_OTHER:				msg << "[Other] "; break;
		default:								msg << "[Unknown Source] "; break;
		}
		switch (type) {
		case GL_DEBUG_TYPE_ERROR:				msg << "[Error] "; break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:	msg << "[Deprecated] "; break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:	msg << "[Undefined] "; break;
		case GL_DEBUG_TYPE_PORTABILITY:			msg << "[Portability] "; break;
		case GL_DEBUG_TYPE_PERFORMANCE:			msg << "[Performance] "; break;
		case GL_DEBUG_TYPE_MARKER:				msg << "[Marker] "; break;
		case GL_DEBUG_TYPE_PUSH_GROUP:			msg << "[Push Group] "; break;
		case GL_DEBUG_TYPE_POP_GROUP:			msg << "[Pop Group] "; break;
		case GL_DEBUG_TYPE_OTHER:				msg << "[Other] "; break;
		default:								msg << "[Unknown Type] "; break;
		}
		switch (severity) {
		case GL_DEBUG_SEVERITY_HIGH:			msg << "[High] "; break;
		case GL_DEBUG_SEVERITY_MEDIUM:			msg << "[Medium] "; break;
		case GL_DEBUG_SEVERITY_LOW:				msg << "[Low] "; break;
		case GL_DEBUG_SEVERITY_NOTIFICATION:	msg << "[Notification] "; break;
		default:								msg << "[Unknown Severity] "; break;
		}
		msg << "ID:" << id << " ";
		if (message && length > 0) {
			msg << std::string_view(message, length);
		}
		switch (severity) {
		case GL_DEBUG_SEVERITY_HIGH:
			if (log::Error) {
				log::Error(msg.str(), std::source_location::current());
			}
			break;
		case GL_DEBUG_SEVERITY_MEDIUM:
			if (log::Warning) {
				log::Warning(msg.str(), std::source_location::current());
			}
			break;
		default:
			if (log::Info) {
				log::Info(msg.str(), std::source_location::current());
			}
			break;
		}
	}

	void InitializeGL() {
#if defined(__ANDROID__)
		if (!gladLoadGLES2(reinterpret_cast<GLADloadfunc>(eglGetProcAddress))) {
			throw std::runtime_error("Failed to load OpenGL ES functions");
		}
#else
		if (!gladLoadGL()) {
#if defined(_WIN32)
			wglMakeCurrent(nullptr, nullptr);
#endif
			throw std::runtime_error("Failed to load GL functions");
		}
#endif
		if (GLAD_GL_KHR_debug || GLAD_GL_ARB_debug_output) {
			glEnable(GL_DEBUG_OUTPUT);
			glDebugMessageCallback(GLDebugMessager, nullptr);
			glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
		}
	}
}

namespace fyuu_rhi::opengl {

#if defined(_WIN32)
	Backend::Instance Backend::CreateInstance(std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver, HWND window_handle) {
		HDC dc = GetDC(window_handle);
		if (!dc) {
			DWORD ec = GetLastError();
			throw std::system_error(ec, std::system_category(), "Failed to get device context");
		}
		PIXELFORMATDESCRIPTOR pfd = {
			sizeof(PIXELFORMATDESCRIPTOR), 1,
			PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
			PFD_TYPE_RGBA, 32, 0,0,0,0,0,0,0,0,
			0,0,0,0,0, 24, 8, 0,
			PFD_MAIN_PLANE, 0,0,0,0
		};
		int pixel_format = ChoosePixelFormat(dc, &pfd);
		if (!pixel_format) {
			DWORD ec = GetLastError();
			throw std::system_error(ec, std::system_category(), "Failed to choose pixel format");
		}
		if (!SetPixelFormat(dc, pixel_format, &pfd)) {
			DWORD ec = GetLastError();
			throw std::system_error(ec, std::system_category(), "Failed to set pixel format");
		}
		HGLRC rc = wglCreateContext(dc);
		if (!rc) {
			DWORD ec = GetLastError();
			throw std::system_error(ec, std::system_category(), "Failed to create rendering context");
		}
		HGLRC shared_rc = wglCreateContext(dc);
		if (!shared_rc) {
			DWORD ec = GetLastError();
			wglDeleteContext(rc);
			throw std::system_error(ec, std::system_category(), "Failed to create the OpenGL shared root context");
		}
		if (!wglShareLists(rc, shared_rc)) {
			DWORD ec = GetLastError();
			wglDeleteContext(shared_rc);
			wglDeleteContext(rc);
			throw std::system_error(ec, std::system_category(), "Failed to initialize the OpenGL context share group");
		}
		if (!wglMakeCurrent(dc, rc)) {
			DWORD ec = GetLastError();
			wglDeleteContext(shared_rc);
			wglDeleteContext(rc);
			throw std::system_error(ec, std::system_category(), "Failed to make context current");
		}
		if (!gladLoadWGL(dc)) {
			wglMakeCurrent(nullptr, nullptr);
			wglDeleteContext(shared_rc);
			wglDeleteContext(rc);
			throw std::runtime_error("Failed to load WGL extensions");
		}
		InitializeGL();
		return { dc, rc, shared_rc };
	}
#elif defined(__linux__)
	Backend::Instance Backend::CreateInstance(std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver, Display* x11_dpy, Window x11_window) {
		static int const attribs[] = {
			GLX_X_RENDERABLE, true,
			GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
			GLX_RENDER_TYPE, GLX_RGBA_BIT,
			GLX_DOUBLEBUFFER, true,
			GLX_RED_SIZE, 8,
			GLX_GREEN_SIZE, 8,
			GLX_BLUE_SIZE, 8,
			GLX_ALPHA_SIZE, 8,
			GLX_DEPTH_SIZE, 24,
			GLX_STENCIL_SIZE, 8,
			None
		};
		int fb_count = 0;
		GLXFBConfig* fbc = glXChooseFBConfig(x11_dpy, DefaultScreen(x11_dpy), attribs, &fb_count);
		if (!fbc || fb_count == 0) {
			throw std::runtime_error("Failed to get GLX framebuffer config");
		}
		GLXFBConfig best_fbc = fbc[0];
		XFree(fbc);
		GLXContext ctx = glXCreateNewContext(x11_dpy, best_fbc, GLX_RGBA_TYPE, nullptr, true);
		if (!ctx) {
			throw std::runtime_error("Failed to create GLX context");
		}
		if (!glXMakeCurrent(x11_dpy, x11_window, ctx)) {
			glXDestroyContext(x11_dpy, ctx);
			throw std::runtime_error("Failed to make GLX context current");
		}

		GLXFBConfig best_fbc = fbc[0];
		XFree(fbc);
		GLXContext ctx = glXCreateNewContext(x11_dpy, best_fbc, GLX_RGBA_TYPE, nullptr, true);
		if (!ctx) {
			throw std::runtime_error("Failed to create GLX context");
		}
		if (!glXMakeCurrent(x11_dpy, x11_window, ctx)) {
			glXDestroyContext(x11_dpy, ctx);
			throw std::runtime_error("Failed to make GLX context current");
		}

		InitializeGL();
		return Instance{ GLX { x11_dpy, x11_window, ctx } };
	}
	Backend::Instance Backend::CreateInstance(std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver, wl_display* display, wl_surface* surface, std::uint32_t width, std::uint32_t height) {
		struct wl_egl_window* egl_window = wl_egl_window_create(surface, width, height);
		if (!egl_window) {
			throw std::runtime_error("Failed to create wl_egl_window");
		}
		EGLDisplay egl_dpy = eglGetDisplay(display);
		if (egl_dpy == EGL_NO_DISPLAY) {
			wl_egl_window_destroy(egl_window);
			throw std::runtime_error("Failed to get EGL display");
		}
		EGLint major, minor;
		if (!eglInitialize(egl_dpy, &major, &minor)) {
			wl_egl_window_destroy(egl_window);
			throw std::runtime_error("Failed to initialize EGL");
		}
		EGLint config_attribs const [] = {
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
			EGL_RED_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8,
			EGL_ALPHA_SIZE, 8,
			EGL_DEPTH_SIZE, 24,
			EGL_STENCIL_SIZE, 8,
			EGL_NONE
		};
		EGLConfig config;
		EGLint num_configs;
		if (!eglChooseConfig(egl_dpy, config_attribs, &config, 1, &num_configs) || num_configs == 0) {
			eglTerminate(egl_dpy);
			wl_egl_window_destroy(egl_window);
			throw std::runtime_error("Failed to choose EGL config");
		}
		EGLSurface egl_surface = eglCreateWindowSurface(egl_dpy, config, egl_window, nullptr);
		if (egl_surface == EGL_NO_SURFACE) {
			eglTerminate(egl_dpy);
			wl_egl_window_destroy(egl_window);
			throw std::runtime_error("Failed to create EGL window surface");
		}
		EGLint context_attribs const [] = {
			EGL_CONTEXT_MAJOR_VERSION, 3,
			EGL_CONTEXT_MINOR_VERSION, 3,
			EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,
			EGL_NONE
		};
		EGLContext egl_ctx = eglCreateContext(egl_dpy, config, EGL_NO_CONTEXT, context_attribs);
		if (egl_ctx == EGL_NO_CONTEXT) {
			eglDestroySurface(egl_dpy, egl_surface);
			eglTerminate(egl_dpy);
			wl_egl_window_destroy(egl_window);
			throw std::runtime_error("Failed to create EGL context");
		}
		if (!eglMakeCurrent(egl_dpy, egl_surface, egl_surface, egl_ctx)) {
			eglDestroyContext(egl_dpy, egl_ctx);
			eglDestroySurface(egl_dpy, egl_surface);
			eglTerminate(egl_dpy);
			wl_egl_window_destroy(egl_window);
			throw std::runtime_error("Failed to make EGL context current");
		}
		InitializeGL();
		return Instance{ EGL{ egl_dpy, egl_surface, egl_surface, egl_ctx, config, egl_window } };
	}
#elif defined(__ANDROID__)
	Backend::Instance Backend::CreateInstance(std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver, android_app* app) {
		EGLDisplay egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
		if (egl_dpy == EGL_NO_DISPLAY) {
			throw std::runtime_error("Failed to get EGL display");
		}
		EGLint major, minor;
		if (!eglInitialize(egl_dpy, &major, &minor)) {
			throw std::runtime_error("Failed to initialize EGL");
		}
		if (!eglBindAPI(EGL_OPENGL_ES_API)) {
			eglTerminate(egl_dpy);
			throw std::runtime_error("Failed to bind OpenGL ES API");
		}
		EGLint config_attribs const [] = {
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
			EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
			EGL_RED_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8,
			EGL_ALPHA_SIZE, 8,
			EGL_DEPTH_SIZE, 24,
			EGL_STENCIL_SIZE, 8,
			EGL_NONE
		};
		EGLConfig config;
		EGLint num_configs;
		if (!eglChooseConfig(egl_dpy, config_attribs, &config, 1, &num_configs) || num_configs == 0) {
			eglTerminate(egl_dpy);
			throw std::runtime_error("Failed to choose EGL config");
		}
		EGLSurface egl_surface = eglCreateWindowSurface(egl_dpy, config, app->window, nullptr);
		if (egl_surface == EGL_NO_SURFACE) {
			eglTerminate(egl_dpy);
			throw std::runtime_error("Failed to create EGL window surface");
		}
		EGLint context_attribs const [] = {
			EGL_CONTEXT_CLIENT_VERSION, 3,
			EGL_NONE
		};
		EGLContext egl_ctx = eglCreateContext(egl_dpy, config, EGL_NO_CONTEXT, context_attribs);
		if (egl_ctx == EGL_NO_CONTEXT) {
			eglDestroySurface(egl_dpy, egl_surface);
			eglTerminate(egl_dpy);
			throw std::runtime_error("Failed to create EGL context");
		}
		if (!eglMakeCurrent(egl_dpy, egl_surface, egl_surface, egl_ctx)) {
			eglDestroyContext(egl_dpy, egl_ctx);
			eglDestroySurface(egl_dpy, egl_surface);
			eglTerminate(egl_dpy);
			throw std::runtime_error("Failed to make EGL context current");
		}
		InitializeGL();
		cache::Initialize(app_name, app_ver, engine_name, engine_ver, app);
		return Instance{ EGL{ egl_dpy, egl_surface, egl_surface, egl_ctx, config, app->window } };
	}
#endif // defined(_WIN32)

	void Backend::ShareContextOnThisThread(Backend::Instance const& instance) {
#if defined(_WIN32)
		thread_local boost::scope::defer_guard ThreadCleanup(CleanupSharedContext);
		if (!s_thread_context) {
			s_thread_device_context = CreateThreadDeviceContext(instance.dc);
			s_thread_context = wglCreateContext(s_thread_device_context);
			if (!s_thread_context) {
				DWORD ec = GetLastError();
				throw std::system_error(ec, std::system_category(), "Failed to create shared GL context");
			}
			if (!wglShareLists(instance.shared_rc, s_thread_context)) {
				DWORD ec = GetLastError();
				wglDeleteContext(s_thread_context);
				s_thread_context = nullptr;
				throw std::system_error(ec, std::system_category(), "Failed to share GL lists");
			}
		}
		if (!wglMakeCurrent(s_thread_device_context, s_thread_context)) {
			DWORD ec = GetLastError();
			throw std::system_error(ec, std::system_category(), "Failed to make shared context current");
		}
#else
		std::visit(
			[&](auto&& arg) {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::std::same_as<T, EGL>) {
					thread_local EGLContext tls_ctx = EGL_NO_CONTEXT;
					thread_local boost::scope::defer_guard tls_cleanup(
						[&]() {
							if (tls_ctx != EGL_NO_CONTEXT) {
								eglMakeCurrent(arg.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
								eglDestroyContext(arg.display, tls_ctx);
								tls_ctx = EGL_NO_CONTEXT;
							}
						}
					);
					if (tls_ctx == EGL_NO_CONTEXT) {
						tls_ctx = eglCreateContext(arg.display, arg.config, arg.context, nullptr);
						if (tls_ctx == EGL_NO_CONTEXT) {
							throw std::runtime_error("Failed to create shared EGL context");
						}
					}
					if (!eglMakeCurrent(arg.display, arg.draw, arg.read, tls_ctx)) {
						throw std::runtime_error("Failed to make shared EGL context current");
					}
				}
#if defined(__linux__)
				else if constexpr (std::std::same_as<T, GLX>) {
					thread_local GLXContext tls_ctx = nullptr;
					thread_local boost::scope::defer_guard tls_cleanup(
						[&]() {
							if (tls_ctx) {
								glXMakeCurrent(arg.dpy, None, nullptr);
								glXDestroyContext(arg.dpy, tls_ctx);
								tls_ctx = nullptr;
							}
						}
					);
					if (!tls_ctx) {
						tls_ctx = glXCreateNewContext(arg.dpy, arg.fbconfig, GLX_RGBA_TYPE, arg.ctx, true);
						if (!tls_ctx) {
							throw std::runtime_error("Failed to create shared GLX context");
						}
					}
					if (!glXMakeCurrent(arg.dpy, arg.drawable, tls_ctx)) {
						throw std::runtime_error("Failed to make shared GLX context current");
					}
				}
#endif // defined(__linux__)
				else {
					throw std::runtime_error("No OpenGL context available");
				}
			},
			instance.gl_handle
		);
#endif
	}

	void Backend::DestroyInstance(Backend::Instance const& instance) noexcept {
#if defined(_WIN32)
		wglMakeCurrent(nullptr, nullptr);
		if (instance.rc) {
			wglDeleteContext(instance.rc);
		}
		if (instance.shared_rc) {
			wglDeleteContext(instance.shared_rc);
		}
#else
		std::visit(
			[&](auto&& arg) {
				using T = std::decay_t<decltype(arg)>;
				if constexpr (std::std::same_as<T, EGL>) {
					if (arg.context != EGL_NO_CONTEXT) {
						eglMakeCurrent(arg.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
						eglDestroyContext(arg.display, arg.context);
					}
					if (arg.draw != EGL_NO_SURFACE) {
						eglDestroySurface(arg.display, arg.draw);
					}
					if (arg.read != EGL_NO_SURFACE && arg.read != arg.draw) {
						eglDestroySurface(arg.display, arg.read);
					}
					if (arg.display != EGL_NO_DISPLAY) {
						eglTerminate(arg.display);
					}
#if defined(__linux__) && !defined(__ANDROID__)
					if (arg.native_window) {
						wl_egl_window_destroy(reinterpret_cast<wl_egl_window*>(arg.native_window));
					}
#elif defined(__ANDROID__)
					(void)arg.native_window;
#endif
				}
#if defined(__linux__)
				else if constexpr (std::std::same_as<T, GLX>) {
					if (arg.ctx) {
						glXMakeCurrent(arg.dpy, None, nullptr);
						glXDestroyContext(arg.dpy, arg.ctx);
					}
				}
#endif // defined(__linux__)
				else {

				}
			},
			instance.gl_handle
		);
#endif
	}

	Backend::PhysicalDevice Backend::EnumeratePhysicalDevices(Backend::Instance const& instance) noexcept {
		return &instance;
	}

}
#endif // !defined(__APPLE__)
