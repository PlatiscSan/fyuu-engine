module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#endif // !defined(__cpp_lib_modules)
#if defined(__linux__) && !defined(__ANDROID__)
#include <boost/scope/defer.hpp>
#include <boost/scope/unique_resource.hpp>
#include <glad/glad.h>
#include <glad/glad_glx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif // defined(__linux__) && !defined(__ANDROID__)

module fyuu_rhi:opengl_instance_glx;
#if defined(__linux__) && !defined(__ANDROID__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :instance_factory;
import :instance_dispatch;
import :opengl_data;
import :opengl_instance_common;
import :physical_device_factory;

namespace {

	constexpr int framebuffer_attributes[]{
		GLX_X_RENDERABLE, True,
		GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT | GLX_PBUFFER_BIT,
		GLX_RENDER_TYPE, GLX_RGBA_BIT,
		GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
		GLX_RED_SIZE, 8,
		GLX_GREEN_SIZE, 8,
		GLX_BLUE_SIZE, 8,
		GLX_ALPHA_SIZE, 8,
		GLX_DEPTH_SIZE, 24,
		GLX_STENCIL_SIZE, 8,
		GLX_DOUBLEBUFFER, True,
		None
	};

	constexpr int context_attributes[]{
		GLX_CONTEXT_MAJOR_VERSION_ARB, 4,
		GLX_CONTEXT_MINOR_VERSION_ARB, 3,
		GLX_CONTEXT_PROFILE_MASK_ARB, GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
#if !defined(NDEBUG)
		GLX_CONTEXT_FLAGS_ARB, GLX_CONTEXT_DEBUG_BIT_ARB,
#endif // !defined(NDEBUG)
		None
	};

	constexpr int pbuffer_attributes[]{
		GLX_PBUFFER_WIDTH, 1,
		GLX_PBUFFER_HEIGHT, 1,
		None
	};

	struct PbufferDeleter {
		Display* display;

		void operator()(GLXPbuffer pbuffer) const noexcept {
			glXDestroyPbuffer(
				display,
				pbuffer
			);
		}
	};

	using ManagedPbuffer = boost::scope::unique_resource<
		GLXPbuffer,
		PbufferDeleter
	>;

	struct ThreadContext {
		ManagedPbuffer pbuffer;
		fyuu_rhi::opengl::ManagedContext context;
	};

	ThreadContext CreateThreadContext(
		fyuu_rhi::opengl::GLXInstance const* instance
	) {
		auto display = instance->display.get();
		auto native_pbuffer = glXCreatePbuffer(
			display,
			instance->config,
			pbuffer_attributes
		);
		if (native_pbuffer == None) {
			throw std::runtime_error("Failed to create the shared GLX pbuffer");
		}
		ManagedPbuffer pbuffer(
			native_pbuffer,
			PbufferDeleter{ display }
		);

		auto native_context = glXCreateContextAttribsARB(
			display,
			instance->config,
			instance->context.get(),
			True,
			context_attributes
		);
		if (!native_context) {
			throw std::runtime_error("Failed to create the shared GLX context");
		}
		fyuu_rhi::opengl::ManagedContext context(
			native_context,
			fyuu_rhi::opengl::ContextDeleter{ display }
		);
		return {
			std::move(pbuffer),
			std::move(context)
		};
	}

} // namespace

namespace fyuu_rhi::opengl {

	void DisplayDeleter::operator()(Display* display) const noexcept {
		XCloseDisplay(display);
	}

	void ColormapDeleter::operator()(Colormap colormap) const noexcept {
		XFreeColormap(
			display,
			colormap
		);
	}

	void DrawableDeleter::operator()(GLXDrawable drawable) const noexcept {
		XDestroyWindow(
			display,
			static_cast<::Window>(drawable)
		);
	}

	void ContextDeleter::operator()(GLXContext context) const noexcept {
		if (glXGetCurrentContext() == context) {
			(void)glXMakeCurrent(
				display,
				None,
				nullptr
			);
		}
		glXDestroyContext(
			display,
			context
		);
	}

} // namespace fyuu_rhi::opengl

namespace fyuu_rhi {

	template <>
	struct CreateInstance<opengl::GLXInstance> {
		opengl::GLXInstance operator()() const {
			static bool const xlib_threading_enabled = XInitThreads() != 0;
			if (!xlib_threading_enabled) {
				throw std::runtime_error("Failed to enable Xlib thread safety");
			}
			auto native_display = XOpenDisplay(nullptr);
			if (!native_display) {
				throw std::runtime_error("Failed to open the X11 display");
			}
			opengl::ManagedDisplay display(
				native_display,
				opengl::DisplayDeleter{}
			);

			auto screen = DefaultScreen(native_display);
			if (!gladLoadGLX(
				native_display,
				screen
			)) {
				throw std::runtime_error("Failed to load GLX functions");
			}
			if (
				!GLAD_GLX_ARB_create_context ||
				!GLAD_GLX_ARB_create_context_profile
			) {
				throw std::runtime_error("GLX_ARB_create_context_profile is unavailable");
			}

			int config_count = 0;
			auto configs = glXChooseFBConfig(
				native_display,
				screen,
				framebuffer_attributes,
				&config_count
			);
			if (!configs || config_count == 0) {
				if (configs) {
					XFree(configs);
				}
				throw std::runtime_error("No compatible GLX framebuffer configuration was found");
			}
			auto config = configs[0];
			XFree(configs);

			auto visual = glXGetVisualFromFBConfig(
				native_display,
				config
			);
			if (!visual) {
				throw std::runtime_error("The GLX framebuffer configuration has no X11 visual");
			}
			boost::scope::defer_guard free_visual(
				[visual]() noexcept {
					XFree(visual);
				}
			);

			auto native_colormap = XCreateColormap(
				native_display,
				RootWindow(native_display, visual->screen),
				visual->visual,
				AllocNone
			);
			if (native_colormap == None) {
				throw std::runtime_error("Failed to create the GLX window colormap");
			}
			opengl::ManagedColormap colormap(
				native_colormap,
				opengl::ColormapDeleter{ native_display }
			);

			XSetWindowAttributes window_attributes{};
			window_attributes.colormap = native_colormap;
			window_attributes.event_mask = StructureNotifyMask;
			auto native_drawable = XCreateWindow(
				native_display,
				RootWindow(native_display, visual->screen),
				0,
				0,
				1u,
				1u,
				0u,
				visual->depth,
				InputOutput,
				visual->visual,
				CWColormap | CWEventMask,
				&window_attributes
			);
			if (native_drawable == None) {
				throw std::runtime_error("Failed to create the GLX instance window");
			}
			opengl::ManagedDrawable drawable(
				native_drawable,
				opengl::DrawableDeleter{ native_display }
			);

			auto native_context = glXCreateContextAttribsARB(
				native_display,
				config,
				nullptr,
				True,
				context_attributes
			);
			if (!native_context) {
				throw std::runtime_error("Failed to create the OpenGL core context through GLX");
			}
			opengl::ManagedContext context(
				native_context,
				opengl::ContextDeleter{ native_display }
			);
			if (!glXMakeCurrent(native_display, native_drawable, native_context)) {
				throw std::runtime_error("Failed to make the GLX context current");
			}

			opengl::InitializeOpenGL();
			return {
				std::move(display),
				config,
				std::move(colormap),
				std::move(drawable),
				std::move(context)
			};
		}
	};

	template <>
	struct ShareContextOnCurrentThread<opengl::GLXInstance> {
		opengl::GLXInstance const* instance;

		void operator()() const {
			thread_local auto context = CreateThreadContext(instance);
			if (!glXMakeContextCurrent(
				instance->display.get(),
				context.pbuffer.get(),
				context.pbuffer.get(),
				context.context.get()
			)) {
				throw std::runtime_error("Failed to make the shared GLX context current");
			}
		}
	};

	template <>
	struct EnumeratePhysicalDevices<opengl::GLXInstance> {
		opengl::GLXInstance const* instance;

		std::vector<PhysicalDevice> operator()() const {
			std::vector<PhysicalDevice> physical_devices;
			physical_devices.emplace_back(MakePhysicalDevice(opengl::PhysicalDevice{ instance }));
			return physical_devices;
		}
	};

} // namespace fyuu_rhi
#endif // defined(__linux__) && !defined(__ANDROID__)
