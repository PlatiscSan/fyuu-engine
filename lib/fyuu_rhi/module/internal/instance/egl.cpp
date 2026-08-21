module;
#include <version>
#if !defined(__cpp_lib_modules)

#include <stdexcept>

#include <string_view>

#include <format>
#endif // !defined(__cpp_lib_modules)
#if defined(__linux__) || defined(__ANDROID__)
#include <glad/glad.h>
#include <glad/glad_egl.h>
#endif // defined(__linux__) || defined(__ANDROID__)

module fyuu_rhi:opengl_instance_egl;
#if defined(__linux__) || defined(__ANDROID__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :instance_factory;
import :instance_dispatch;
import :opengl_data;
import :opengl_instance_common;
import :physical_device_factory;

namespace {

	void ThrowEGL(std::string_view operation) {
		throw std::runtime_error(
			std::format(
				"{} failed with EGL error 0x{:04X}",
				operation,
				static_cast<unsigned int>(eglGetError())
			)
		);
	}

	bool HasExtension(char const* extensions, std::string_view extension) noexcept {
		if (!extensions || extension.empty()) {
			return false;
		}
		std::string_view available(extensions);
		auto offset = available.find(extension);
		while (offset != std::string_view::npos) {
			auto left_matches = offset == 0u || available[offset - 1u] == ' ';
			auto end = offset + extension.size();
			auto right_matches = end == available.size() || available[end] == ' ';
			if (left_matches && right_matches) {
				return true;
			}
			offset = available.find(extension, end);
		}
		return false;
	}

	EGLDisplay CreateDisplay() {
#if defined(__linux__) && !defined(__ANDROID__)
		auto client_extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
		if (HasExtension(client_extensions,"EGL_EXT_platform_base") &&
			HasExtension(client_extensions,"EGL_MESA_platform_surfaceless") &&
			glad_eglGetPlatformDisplayEXT
		) {
			auto display = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
			if (display != EGL_NO_DISPLAY) {
				return display;
			}
		}
#endif // defined(__linux__) && !defined(__ANDROID__)
		return eglGetDisplay(EGL_DEFAULT_DISPLAY);
	}

#if defined(__ANDROID__)
	constexpr EGLint config_attributes[]{
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 24,
		EGL_STENCIL_SIZE, 8,
		EGL_NONE
	};

	constexpr EGLint context_attributes[]{
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_NONE
	};
#else
	constexpr EGLint config_attributes[]{
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_RED_SIZE, 8,
		EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 8,
		EGL_DEPTH_SIZE, 24,
		EGL_STENCIL_SIZE, 8,
		EGL_NONE
	};

	constexpr EGLint context_attributes[]{
		EGL_CONTEXT_MAJOR_VERSION_KHR, 4,
		EGL_CONTEXT_MINOR_VERSION_KHR, 3,
		EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR,
		EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR,
#if !defined(NDEBUG)
		EGL_CONTEXT_FLAGS_KHR, EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR,
#endif // !defined(NDEBUG)
		EGL_NONE
	};
#endif // defined(__ANDROID__)

	constexpr EGLint surface_attributes[]{
		EGL_WIDTH, 1,
		EGL_HEIGHT, 1,
		EGL_NONE
	};

	struct ThreadContext {
		fyuu_rhi::opengl::ManagedEGLSurface surface;
		fyuu_rhi::opengl::ManagedEGLContext context;
	};

	template <class Instance>
	ThreadContext CreateThreadContext(Instance const* instance) {
		auto display = instance->display.get();
#if defined(__ANDROID__)
		if (!eglBindAPI(EGL_OPENGL_ES_API)) {
			ThrowEGL("eglBindAPI(EGL_OPENGL_ES_API)");
		}
#else
		if (!eglBindAPI(EGL_OPENGL_API)) {
			ThrowEGL("eglBindAPI(EGL_OPENGL_API)");
		}
#endif // defined(__ANDROID__)

		auto native_surface = eglCreatePbufferSurface(
			display,
			instance->config,
			surface_attributes
		);
		if (native_surface == EGL_NO_SURFACE) {
			ThrowEGL("eglCreatePbufferSurface");
		}
		fyuu_rhi::opengl::ManagedEGLSurface surface(
			native_surface,
			fyuu_rhi::opengl::EGLSurfaceDeleter{ display }
		);

		auto native_context = eglCreateContext(
			display,
			instance->config,
			instance->context.get(),
			context_attributes
		);
		if (native_context == EGL_NO_CONTEXT) {
			ThrowEGL("eglCreateContext");
		}
		fyuu_rhi::opengl::ManagedEGLContext context(
			native_context,
			fyuu_rhi::opengl::EGLContextDeleter{ display }
		);
		return {
			std::move(surface),
			std::move(context)
		};
	}

	template <class Instance>
	Instance CreateEGLInstance() {
		if (!gladLoadEGL()) {
			throw std::runtime_error("Failed to load EGL functions");
		}

		auto native_display = CreateDisplay();
		if (native_display == EGL_NO_DISPLAY) {
			ThrowEGL("eglGetDisplay");
		}
		fyuu_rhi::opengl::ManagedEGLDisplay display(
			native_display,
			fyuu_rhi::opengl::EGLDisplayDeleter{}
		);

		EGLint major = 0;
		EGLint minor = 0;
		if (!eglInitialize(native_display, &major, &minor)) {
			ThrowEGL("eglInitialize");
		}

#if defined(__ANDROID__)
		if (!eglBindAPI(EGL_OPENGL_ES_API)) {
			ThrowEGL("eglBindAPI(EGL_OPENGL_ES_API)");
		}
#else
		auto display_extensions = eglQueryString(native_display, EGL_EXTENSIONS);

		if ((major < 1 || (major == 1 && minor < 5)) && !HasExtension(display_extensions, "EGL_KHR_create_context")) {
			throw std::runtime_error("EGL 1.5 or EGL_KHR_create_context is required");
		}
		if (!eglBindAPI(EGL_OPENGL_API)) {
			ThrowEGL("eglBindAPI(EGL_OPENGL_API)");
		}
#endif // defined(__ANDROID__)

		EGLConfig config = nullptr;
		EGLint config_count = 0;
		if (!eglChooseConfig(native_display, config_attributes, &config, 1, &config_count)) {
			ThrowEGL("eglChooseConfig");
		}
		if (config_count == 0) {
			throw std::runtime_error("No compatible EGL configuration was found");
		}

		auto native_surface = eglCreatePbufferSurface(native_display, config, surface_attributes);
		if (native_surface == EGL_NO_SURFACE) {
			ThrowEGL("eglCreatePbufferSurface");
		}
		fyuu_rhi::opengl::ManagedEGLSurface surface(
			native_surface,
			fyuu_rhi::opengl::EGLSurfaceDeleter{ native_display }
		);

		auto native_context = eglCreateContext(native_display, config, EGL_NO_CONTEXT, context_attributes);

		if (native_context == EGL_NO_CONTEXT) {
			ThrowEGL("eglCreateContext");
		}

		fyuu_rhi::opengl::ManagedEGLContext context(native_context, fyuu_rhi::opengl::EGLContextDeleter{ native_display });

		if (!eglMakeCurrent(native_display, native_surface, native_surface, native_context)) {
			ThrowEGL("eglMakeCurrent");
		}

		fyuu_rhi::opengl::InitializeOpenGL();
		return {
			std::move(display),
			config,
			std::move(surface),
			std::move(context)
		};
	}

} // namespace

namespace fyuu_rhi::opengl {

	void EGLDisplayDeleter::operator()(EGLDisplay display) const noexcept {
		(void)eglTerminate(display);
	}

	void EGLSurfaceDeleter::operator()(EGLSurface surface) const noexcept {
		(void)eglDestroySurface(
			display,
			surface
		);
	}

	void EGLContextDeleter::operator()(EGLContext context) const noexcept {
		if (eglGetCurrentContext() == context) {
			(void)eglMakeCurrent(
				display,
				EGL_NO_SURFACE,
				EGL_NO_SURFACE,
				EGL_NO_CONTEXT
			);
		}
		(void)eglDestroyContext(
			display,
			context
		);
	}

} // namespace fyuu_rhi::opengl

namespace fyuu_rhi {

#if defined(__linux__) && !defined(__ANDROID__)
	template <>
	struct CreateInstance<opengl::EGLInstance> {
		opengl::EGLInstance operator()() const {
			return CreateEGLInstance<opengl::EGLInstance>();
		}
	};

	template <>
	struct ShareContextOnCurrentThread<opengl::EGLInstance> {
		opengl::EGLInstance const* instance;

		void operator()() const {
			thread_local auto context = CreateThreadContext(instance);
			if (!eglMakeCurrent(
				instance->display.get(),
				context.surface.get(),
				context.surface.get(),
				context.context.get()
			)) {
				ThrowEGL("eglMakeCurrent");
			}
		}
	};

	template <>
	struct EnumeratePhysicalDevices<opengl::EGLInstance> {
		opengl::EGLInstance const* instance;

		std::vector<PhysicalDevice> operator()() const {
			std::vector<PhysicalDevice> physical_devices;
			physical_devices.emplace_back(
				MakePhysicalDevice(opengl::PhysicalDevice{ instance })
			);
			return physical_devices;
		}
	};
#else
	template <>
	struct CreateInstance<opengl::Instance> {
		opengl::Instance operator()() const {
			return CreateEGLInstance<opengl::Instance>();
		}
	};

	template <>
	struct ShareContextOnCurrentThread<opengl::Instance> {
		opengl::Instance const* instance;

		void operator()() const {
			thread_local auto context = CreateThreadContext(instance);
			if (!eglMakeCurrent(
				instance->display.get(),
				context.surface.get(),
				context.surface.get(),
				context.context.get()
			)) {
				ThrowEGL("eglMakeCurrent");
			}
		}
	};

	template <>
	struct EnumeratePhysicalDevices<opengl::Instance> {
		opengl::Instance const* instance;

		std::vector<PhysicalDevice> operator()() const {
			std::vector<PhysicalDevice> physical_devices;
			physical_devices.emplace_back(MakePhysicalDevice(opengl::PhysicalDevice{ instance }));
			return physical_devices;
		}
	};
#endif // defined(__linux__) && !defined(__ANDROID__)

} // namespace fyuu_rhi
#endif // defined(__linux__) || defined(__ANDROID__)
