module;

#if !defined(ENGINE_VER_VARIANT)
#define ENGINE_VER_VARIANT 0
#endif // !defined(ENGINE_VER_VARIANT)

#if !defined(ENGINE_VER_MAJOR)
#define ENGINE_VER_MAJOR 1
#endif // !defined(ENGINE_VER_MAJOR)

#if !defined(ENGINE_VER_MINOR)
#define ENGINE_VER_MINOR 0
#endif // !defined(ENGINE_VER_MINOR)

#if !defined(ENGINE_VER_PATCH)
#define ENGINE_VER_PATCH 0
#endif // !defined(ENGINE_VER_PATCH)

#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <string>
#include <string_view>
#include <filesystem>
#include <source_location>
#include <format>
#include <print>
#endif // !defined(__cpp_lib_modules)
#include <SDL3/SDL.h>
#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <wayland-client-core.h>
#include <wayland-util.h>
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#elif defined(__ANDROID__)
#include <android/native_window.h>
#endif // defined(_WIN32)
#include "fyuu_application.h"
export module fyuu_engine:renderer_instance;
#if defined(__cpp_lib_modules)	
import std;
#endif // defined(__cpp_lib_modules)
import :log;
import fyuu_rhi;

namespace {

	using namespace fyuu_engine;

	void InitializeRHILogger() {
		fyuu_rhi::log::Trace = log::Trace;
		fyuu_rhi::log::Info = log::Info;
		fyuu_rhi::log::Warning = log::Warning;
		fyuu_rhi::log::Error = log::Error;
		fyuu_rhi::log::Fatal = log::Fatal;
	}

	template <class PhysDev>
	void LogPhysicalDevice(PhysDev&& phys_dev, std::source_location const& loc = std::source_location::current()) {

		auto info = std::forward<PhysDev>(phys_dev).GetInfo();

		auto TypeToString = [](fyuu_rhi::PhysicalDeviceInfo::Type type) -> std::string_view {
			using enum fyuu_rhi::PhysicalDeviceInfo::Type;
			switch (type) {
			case Unknown:
				return "Unknown";
			case DiscreteGPU:
				return "Discrete GPU";
			case IntegratedGPU:
				return "Integrated GPU";
			case CPU:
				return "CPU";
			case Virtual:
				return "Virtual";
			default:
				return "?";
			}
			};

		auto FormatMemory = [](std::optional<std::size_t> const& bytes) -> std::string {
			if (!bytes) {
				return "N/A";
			}
			std::size_t b = *bytes;
			if (b < 1024) {
				return std::format("{} B", b);
			}
			else if (b < 1024 * 1024) {
				return std::format("{:.2f} KB", b / 1024.0);
			}
			else if (b < 1024 * 1024 * 1024) {
				return std::format("{:.2f} MB", b / (1024.0 * 1024.0));
			}
			else {
				return std::format("{:.2f} GB", b / (1024.0 * 1024.0 * 1024.0));
			}
			};

		auto FormatHex = [](std::optional<std::uint32_t> const& val) -> std::string {
			if (!val) {
				return "N/A";
			}
			return std::format("0x{:04X}", *val);
			};

		std::string msg = std::format(
			"\nPhysical device info:\n"
			"  Name:             {}\n"
			"  Type:             {}\n"
			"  Vendor ID:        {}\n"
			"  Device ID:        {}\n"
			"  Dedicated Memory: {}",
			info.name,
			TypeToString(info.type),
			FormatHex(info.vendor_id),
			FormatHex(info.device_id),
			FormatMemory(info.dedicated_memory)
		);

		log::Info(msg, loc);
	}

#if defined(_WIN32)
	HWND GetNativeWindowHandle(SDL_Window* window) {
		SDL_PropertiesID props = SDL_GetWindowProperties(window);
		if (!props) {
			throw std::runtime_error(std::format("Calling SDL_GetWindowProperties(), SDL reported: {}", SDL_GetError()));
		}
		auto native = static_cast<HWND>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
		if (!native) {
			throw std::runtime_error(std::format("Calling SDL_GetPointerProperty() for HWND, SDL reported: {}", SDL_GetError()));
		}
		return native;
	}

	void InitializeD3D12(SDL_Window* window, Fyuu_App* app) {
		fyuu_rhi::D3D12Instance::Initialize();
		fyuu_rhi::D3D12Instance* instance = fyuu_rhi::D3D12Instance::Get();
		std::vector<fyuu_rhi::D3D12PhysicalDevice> phys_devs = instance->EnumeratePhysicalDevices();
		fyuu_rhi::D3D12PhysicalDevice best_phys_dev = fyuu_rhi::BestPerformance(phys_devs);
		LogPhysicalDevice(best_phys_dev);

		fyuu_rhi::D3D12LogicalDevice logical_dev = best_phys_dev.CreateLogicalDevice();
	}
#endif // defined(_WIN32)

#if defined(__linux__)
	class NotX11 : public std::runtime_error {
	public:
		NotX11() : std::runtime_error("Not an X11 window") {}
	};

	std::pair<Display*, Window> GetX11NativeHandle(SDL_Window* window) {
		SDL_PropertiesID props = SDL_GetWindowProperties(window);
		if (!props) {
			throw std::runtime_error(std::format("Calling SDL_GetWindowProperties(), SDL reported: {}", SDL_GetError()));
		}
		auto display = static_cast<Display*>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr));
		if (!display) {
			throw NotX11{};
		}
		auto window_id = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
		if (window_id == 0) {
			throw std::runtime_error(std::format("Calling SDL_GetNumberProperty() for X11 window number, SDL reported: {}", SDL_GetError()));
		}
		return { display, static_cast<Window>(window_id) };
	}

	std::pair<wl_display*, wl_surface*> GetWaylandNativeHandle(SDL_Window* window) {
		SDL_PropertiesID props = SDL_GetWindowProperties(window);
		if (!props) {
			throw std::runtime_error(std::format("Calling SDL_GetWindowProperties(), SDL reported: {}", SDL_GetError()));
		}
		auto display = static_cast<wl_display*>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr));
		if (!display) {
			throw std::runtime_error(std::format("Calling SDL_GetPointerProperty() for Wayland display, SDL reported: {}", SDL_GetError()));
		}
		auto surface = static_cast<wl_surface*>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr));
		if (!surface) {
			throw std::runtime_error(std::format("Calling SDL_GetPointerProperty() for Wayland surface, SDL reported: {}", SDL_GetError()));
		}
		return { display, surface };
	}
#endif // defined(__linux__)

#if defined(__ANDROID__)
	ANativeWindow* GetAndroidNativeWindow(SDL_Window* window) {
		SDL_PropertiesID props = SDL_GetWindowProperties(window);
		if (!props) {
			throw std::runtime_error(std::format("Calling SDL_GetWindowProperties(), SDL reported: {}", SDL_GetError()));
		}
		auto native_window = static_cast<ANativeWindow*>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr));
		if (!native_window) {
			throw std::runtime_error(std::format("Calling SDL_GetPointerProperty() for Android native window, SDL reported: {}", SDL_GetError()));
		}
		return native_window;
	}
#endif // defined(__ANDROID__)

#if defined(__APPLE__)
	void InitializeMetal(SDL_Window* window, Fyuu_App* app) {

	}
#else
	void InitializeVulkan(SDL_Window* window, Fyuu_App* app) {

		static constexpr fyuu_rhi::Version engine_ver = {
			.variant = ENGINE_VER_VARIANT,
			.major = ENGINE_VER_MAJOR,
			.minor = ENGINE_VER_MINOR,
			.patch = ENGINE_VER_PATCH
		};

		auto app_ver = reinterpret_cast<fyuu_rhi::Version*>(&app->version);

		fyuu_rhi::VulkanInstance::Initialize(app->name, *app_ver, "Fyuu Engine", engine_ver);
		fyuu_rhi::VulkanInstance* instance = fyuu_rhi::VulkanInstance::Get();
		std::vector<fyuu_rhi::VulkanPhysicalDevice> phys_devs = instance->EnumeratePhysicalDevices();
		fyuu_rhi::VulkanPhysicalDevice best_phys_dev = fyuu_rhi::BestPerformance(phys_devs);
		LogPhysicalDevice(best_phys_dev);

		fyuu_rhi::VulkanLogicalDevice logical_dev = best_phys_dev.CreateLogicalDevice();
	}

	void InitializeOpenGL(SDL_Window* window, Fyuu_App* app) {
#if defined(_WIN32)
		HWND native_window = GetNativeWindowHandle(window);
		fyuu_rhi::OpenGLInstance::Initialize(native_window);
#elif defined(__linux__)
		try {
			auto [display, window_id] = GetX11NativeHandle(window);
			fyuu_rhi::OpenGLInstance::Initialize(display, window_id);
		}
		catch (NotX11 const&) {
			log::Warning(std::format("Failed to get X11 native handle: {}, trying Wayland...", ex.what()));
			auto [display, surface] = GetWaylandNativeHandle(window);
			fyuu_rhi::OpenGLInstance::Initialize(display, surface, app->surface_width, app->surface_height);
		}
#elif defined(__ANDROID__)
		ANativeWindow* native_window = GetAndroidNativeWindow(window);
		fyuu_rhi::OpenGLInstance::Initialize(native_window);
#endif // defined(_WIN32)
		fyuu_rhi::OpenGLPhysicalDevice best_phys_dev = fyuu_rhi::OpenGLInstance::Get()->EnumeratePhysicalDevices();
		LogPhysicalDevice(best_phys_dev);
	}
#endif // defined(__APPLE__)
	void InitializeWebGPU(SDL_Window* window, Fyuu_App* app) {

		fyuu_rhi::WebGPUInstance::Initialize();
		fyuu_rhi::WebGPUInstance* instance = fyuu_rhi::WebGPUInstance::Get();
		fyuu_rhi::WebGPUPhysicalDevice phys_dev = instance->EnumeratePhysicalDevices();
		LogPhysicalDevice(phys_dev);

		fyuu_rhi::WebGPULogicalDevice logical_dev = phys_dev.CreateLogicalDevice();
	}

	void InitializePlatformDefault(SDL_Window* window, Fyuu_App* app) {
#if defined(_WIN32)
		InitializeD3D12(window, app);
#elif defined(__APPLE__)
		InitializeMetal(window, app);
#elif defined(__linux__) || defined(__ANDROID__)
		InitializeVulkan(window, app);
#else
#error "Unsupported platform for default renderer"
#endif // defined(_WIN32)
	}

	constexpr std::size_t HashCString(std::string_view sv) noexcept {
		std::size_t hash = 14695981039346656037ULL; // FNV offset basis
		for (char c : sv) {
			hash ^= static_cast<std::size_t>(c);
			hash *= 1099511628211ULL; // FNV prime
		}
		return hash;
	}

}

namespace fyuu_engine::rendering {

	export void Initialize(SDL_Window* window, Fyuu_App* app, std::string_view graphics_api) {

		using InitFunc = void(*)(SDL_Window*, Fyuu_App*);

		struct APIEntry {
			std::size_t hash;
			InitFunc Init;
		};

		using Table = std::array<APIEntry, 5u>;

		static constexpr Table api_table = []() {
			Table table{};
			std::size_t idx = 0;

			auto Add = [&](std::string_view name, InitFunc f) {
				table[idx++] = { HashCString(name), f };
				};

			Add("platformdefault", InitializePlatformDefault);
			Add("webgpu", InitializeWebGPU);
#if defined(_WIN32)
			Add("d3d12", InitializeD3D12);
			Add("vulkan", InitializeVulkan);
			Add("opengl", InitializeOpenGL);
#elif defined(__APPLE__)
			Add("metal", InitializeMetal);
#elif defined(__linux__)
			Add("vulkan", InitializeVulkan);
			Add("opengl", InitializeOpenGL);
#elif defined(__ANDROID__)
			Add("vulkan", InitializeVulkan);
			Add("opengl", InitializeOpenGL);
#endif // defined(_WIN32)
			return table;
			}();

		InitFunc Init = nullptr;
		std::size_t hash = HashCString(graphics_api);
		for (auto const& entry : api_table) {
			if (entry.hash == hash) {
				Init = entry.Init;
				break;
			}
		}

		if (!Init) {
			throw std::runtime_error("rendering::Initialize(): Unknown graphics API to initialize with");
		}

		InitializeRHILogger();
		Init(window, app);

	}

	export void Shutdown() {

	}

};