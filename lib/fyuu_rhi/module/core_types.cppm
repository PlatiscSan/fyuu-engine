/* common pattern
module;
#include <version>
#if !defined(__cpp_lib_modules)

#endif // !defined(__cpp_lib_modules)

export module fyuu_rhi:;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
namespace fyuu_rhi {

}
*/
module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>

#include <optional>
#include <variant>

#include <compare>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#elif defined(__ANDROID__)
#include <android/native_window.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <wayland-client-core.h>
#endif // defined(_WIN32)
export module fyuu_rhi:core_types;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
namespace fyuu_rhi {

#if defined(_WIN32)
	export using PlatformHandle = HWND;
#elif defined(__ANDROID__)
	export using PlatformHandle = ANativeWindow*;
#elif defined(__linux__)
	export struct X11PlatformHandle {
		Display* display;
		Window window;

		std::strong_ordering operator<=>(X11PlatformHandle const&) const noexcept = default;
	};

	export struct WaylandPlatformHandle {
		wl_display* display;
		wl_surface* surface;

		std::strong_ordering operator<=>(WaylandPlatformHandle const&) const noexcept = default;
	};

	export using PlatformHandle = std::variant<X11PlatformHandle, WaylandPlatformHandle>;
#elif defined(__APPLE__)
	export using PlatformHandle = void*;
#endif // defined(_WIN32)
	
	export struct Version {
		std::uint8_t variant;
		std::uint8_t major;
		std::uint8_t minor;
		std::uint8_t patch;
	};

	/// The clip-space convention the application authors its content in. The RHI
	/// normalizes each backend to the requested orientation so shaders are portable.
	/// Handedness (left/right-handed) is expressed by the app's projection matrix
	/// and is not part of rasterization, so it is not represented here.
	export enum class ClipSpace : std::uint8_t {
		/// NDC +Y points toward the top of the framebuffer (D3D12, WebGPU and OpenGL
		/// convention). Vulkan backends compensate with a flipped viewport.
		YUp,
		/// Use each backend's native NDC (Vulkan: +Y down; D3D12/WebGPU/GL: +Y up).
		ApiNative
	};

	export struct PhysicalDeviceInfo {
		
		enum class Type : std::uint8_t {
			Unknown,
			DiscreteGPU, 
			IntegratedGPU, 
			CPU, 
			Virtual
		};

		std::string name;
		std::optional<std::uint32_t> vendor_id;
		std::optional<std::uint32_t> device_id;
		std::optional<std::size_t> dedicated_memory;
		Type type;
	};

}
