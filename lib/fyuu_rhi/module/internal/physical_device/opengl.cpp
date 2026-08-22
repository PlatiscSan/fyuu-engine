module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <stdexcept>

#include <vector>

#include <algorithm>

#include <cstdint>

#include <optional>
#include <string_view>

#include <ranges>
#endif // !defined(__cpp_lib_modules)
#include <frozen/map.h>
#if defined(_WIN32)
#include <Windows.h>
#include <glad/glad.h>
#include <glad/glad_wgl.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <glad/glad.h>
#include <glad/glad_egl.h>
#include <glad/glad_glx.h>
#include <X11/Xlib.h>
#elif defined(__ANDROID__)
#include <glad/glad.h>
#include <glad/glad_egl.h>
#endif // defined(_WIN32)

module fyuu_rhi:opengl_physical_device;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :logical_device_factory;
import :opengl_data;
import :physical_device_dispatch;
import :physical_device_factory;
import :physical_device;
#endif // !defined(__APPLE__)

namespace {
	char const* GetRendererName() {
		char const* renderer = reinterpret_cast<char const*>(glGetString(GL_RENDERER));
		return renderer ? renderer : "OpenGL renderer";
	}

	std::optional<std::uint32_t> GetVendorID() {
		auto native_vendor = reinterpret_cast<char const*>(glGetString(GL_VENDOR));
		if (!native_vendor) {
			return std::nullopt;
		}
		static constexpr frozen::map<
			std::string_view,
			std::uint32_t,
			6u
		> vendor_ids{
			{ "AMD", 0x1002u },
			{ "ARM", 0x13B5u },
			{ "ATI", 0x1002u },
			{ "Intel", 0x8086u },
			{ "NVIDIA", 0x10DEu },
			{ "Qualcomm", 0x5143u }
		};
		std::string_view vendor(native_vendor);
		auto result = std::ranges::find_if(
			vendor_ids,
			[vendor](auto const& entry) {
				return vendor.contains(entry.first);
			}
		);
		if (result != vendor_ids.end()) {
			return result->second;
		}
		return std::nullopt;
	}
}

namespace fyuu_rhi {
	template <>
	struct GetPhysicalDeviceInfo<opengl::PhysicalDevice> {
		opengl::PhysicalDevice const* native;
		PhysicalDevice::Info operator()() {
			return {
				.name = GetRendererName(),
				.vendor_id = GetVendorID(),
				.device_id = std::nullopt,
				.dedicated_memory = std::nullopt,
				.type = PhysicalDevice::Info::Type::Unknown
			};
		}
	};

	template <>
	struct CreateLogicalDevice<opengl::PhysicalDevice> {
		opengl::PhysicalDevice const* physical_device;

		LogicalDevice operator()() const {
			// The window + GL context already live on the backend instance; the
			// logical device just carries a reference to it for the execution path.
			return MakeLogicalDevice(
				opengl::LogicalDevice{ physical_device->instance }
			);
		}
	};
}
