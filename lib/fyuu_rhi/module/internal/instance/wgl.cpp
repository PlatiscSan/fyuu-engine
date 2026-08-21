module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>

#include <array>

#include <mutex>
#include <system_error>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#include <boost/scope/defer.hpp>
#include <boost/scope/unique_resource.hpp>
#include <glad/glad.h>
#include <glad/glad_wgl.h>
#endif // defined(_WIN32)

module fyuu_rhi:opengl_instance_wgl;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :instance_factory;
import :instance_dispatch;
import :opengl_data;
import :opengl_instance_common;
import :physical_device_factory;

namespace {
	constexpr int context_attributes[]{
		WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
		WGL_CONTEXT_MINOR_VERSION_ARB, 3,
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
#if !defined(NDEBUG)
		WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
#endif // !defined(NDEBUG)
		0
	};

	struct DeviceContextDeleter {
		HWND window;

		void operator()(HDC device_context) const noexcept {
			(void)ReleaseDC(
				window,
				device_context
			);
		}
	};

	struct RenderingContextDeleter {
		void operator()(HGLRC context) const noexcept {
			if (wglGetCurrentContext() == context) {
				(void)wglMakeCurrent(
					nullptr,
					nullptr
				);
			}
			(void)wglDeleteContext(context);
		}
	};

	using ManagedDeviceContext = boost::scope::unique_resource<
		HDC,
		DeviceContextDeleter
	>;
	using ManagedRenderingContext = boost::scope::unique_resource<
		HGLRC,
		RenderingContextDeleter
	>;

	struct ThreadContext {
		fyuu_rhi::opengl::Window window;
		ManagedDeviceContext device_context;
		ManagedRenderingContext context;
	};

	fyuu_rhi::opengl::Window CreateInstanceWindow() {
		static constexpr TCHAR class_name[] = TEXT("FyuuRHI.OpenGL.Instance");
		static std::once_flag registration;
		static ATOM atom;
		std::call_once(
			registration,
			[]() {
				WNDCLASS description{
					.style = CS_OWNDC,
					.lpfnWndProc = DefWindowProc,
					.hInstance = GetModuleHandle(nullptr),
					.lpszClassName = class_name
				};
				atom = RegisterClass(&description);
				if (!atom) {
					throw std::system_error(
						static_cast<int>(GetLastError()),
						std::system_category(),
						"Failed to register the OpenGL instance window class"
					);
				}
				static boost::scope::defer_guard unregister_class(
					[]() noexcept {
						UnregisterClass(class_name, GetModuleHandle(nullptr));
					}
				);
			}
		);
		auto window = CreateWindowEx(
			0u,
			class_name,
			TEXT("FyuuRHI OpenGL Instance"),
			WS_POPUP,
			0,
			0,
			1,
			1,
			nullptr,
			nullptr,
			GetModuleHandle(nullptr),
			nullptr
		);
		if (!window) {
			throw std::system_error(
				static_cast<int>(GetLastError()),
				std::system_category(),
				"Failed to create the OpenGL instance window"
			);
		}
		return fyuu_rhi::opengl::Window(window);
	}

	void ConfigurePixelFormat(HDC device_context) {
		PIXELFORMATDESCRIPTOR description{
			.nSize = sizeof(PIXELFORMATDESCRIPTOR),
			.nVersion = 1u,
			.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
			.iPixelType = PFD_TYPE_RGBA,
			.cColorBits = 32u,
			.cDepthBits = 24u,
			.cStencilBits = 8u,
			.iLayerType = PFD_MAIN_PLANE
		};
		auto format = ChoosePixelFormat(
			device_context,
			&description
		);
		if (
			!format ||
			!SetPixelFormat(
				device_context,
				format,
				&description
			)
		) {
			throw std::runtime_error("Failed to configure the OpenGL pixel format");
		}
	}

	ThreadContext CreateThreadContext(fyuu_rhi::opengl::Instance const* instance) {
		auto window = CreateInstanceWindow();
		auto native_device_context = GetDC(window.get());
		if (!native_device_context) {
			throw std::system_error(
				static_cast<int>(GetLastError()),
				std::system_category(),
				"Failed to acquire the OpenGL thread device context"
			);
		}
		ManagedDeviceContext device_context(
			native_device_context,
			DeviceContextDeleter{ window.get() }
		);
		ConfigurePixelFormat(native_device_context);

		auto native_context = wglCreateContextAttribsARB(
			native_device_context,
			instance->context,
			context_attributes
		);
		if (!native_context) {
			throw std::runtime_error("Failed to create the shared WGL context");
		}
		ManagedRenderingContext context(
			native_context,
			RenderingContextDeleter{}
		);
		return {
			std::move(window),
			std::move(device_context),
			std::move(context)
		};
	}

} // namespace

namespace fyuu_rhi {
	namespace opengl {
		void WindowDeleter::operator()(HWND window) const noexcept {
			DestroyWindow(window);
		}
	} // namespace opengl

	template <>
	struct CreateInstance<opengl::Instance> {
		opengl::Instance operator()() const {

			auto window = CreateInstanceWindow();
			auto device_context = GetDC(window.get());
			if (!device_context) {
				throw std::system_error(
					static_cast<int>(GetLastError()),
					std::system_category(),
					"Failed to acquire the OpenGL instance device context"
				);
			}
			try {
				ConfigurePixelFormat(device_context);
			}
			catch (...) {
				ReleaseDC(window.get(), device_context);
				throw;
			}
			auto bootstrap = wglCreateContext(device_context);
			if (!bootstrap || !wglMakeCurrent(device_context, bootstrap)) {
				if (bootstrap) {
					wglDeleteContext(bootstrap);
				}
				ReleaseDC(window.get(), device_context);
				throw std::runtime_error("Failed to create the OpenGL bootstrap context");
			}
			if (!gladLoadWGL(device_context) || !WGL_ARB_create_context) {
				wglMakeCurrent(nullptr, nullptr);
				wglDeleteContext(bootstrap);
				ReleaseDC(window.get(), device_context);
				throw std::runtime_error("WGL_ARB_create_context is unavailable");
			}
			auto context = wglCreateContextAttribsARB(
				device_context,
				nullptr,
				context_attributes
			);
			if (!context || !wglMakeCurrent(device_context, context)) {
				wglMakeCurrent(nullptr, nullptr);
				if (context) {
					wglDeleteContext(context);
				}
				wglDeleteContext(bootstrap);
				ReleaseDC(window.get(), device_context);
				throw std::runtime_error("Failed to create the OpenGL core context");
			}
			wglDeleteContext(bootstrap);
			opengl::InitializeOpenGL();
			return {
				std::move(window),
				device_context,
				context,
				GetCurrentThreadId()
			};
		}
	};

	template <>
	struct ShareContextOnCurrentThread<opengl::Instance> {
		opengl::Instance const* instance;

		void operator()() const {
			thread_local auto context = CreateThreadContext(instance);
			if (!wglMakeCurrent(context.device_context.get(), context.context.get())) {
				throw std::runtime_error("Failed to make the shared WGL context current");
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

} // namespace fyuu_rhi
#endif // defined(_WIN32)
