module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <optional>
#include <string_view>
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
#include <android/native_window.h>
#include <android/android_native_app_glue.h>

#endif // defined(_WIN32)
#endif // !defined(__APPLE__)
module fyuu_rhi:opengl_physical_device;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core_types;
import :opengl_traits;

namespace {

	char const* GetRendererName() {
		char const* renderer = reinterpret_cast<char const*>(glGetString(GL_RENDERER));
		return renderer ? renderer : "OpenGL renderer";
	}

	std::optional<std::uint32_t> GetVendorID() {
		std::string_view vendor(reinterpret_cast<char const*>(glGetString(GL_VENDOR)));
		if (vendor.empty()) return std::nullopt;
		if (vendor.find("NVIDIA") != std::string_view::npos) return 0x10DE;
		if (vendor.find("AMD") != std::string_view::npos || vendor.find("ATI") != std::string_view::npos) return 0x1002;
		if (vendor.find("Intel") != std::string_view::npos) return 0x8086;
		if (vendor.find("ARM") != std::string_view::npos) return 0x13B5;
		if (vendor.find("Qualcomm") != std::string_view::npos) return 0x5143;
		return std::nullopt;
	}

}

namespace fyuu_rhi::opengl {

	PhysicalDeviceInfo Backend::GetPhysicalDeviceInfo(Backend::PhysicalDevice const& phys_dev) {

		PhysicalDeviceInfo info = {
			.name = GetRendererName(),
			.vendor_id = GetVendorID(),
			.device_id = std::nullopt,
			.dedicated_memory = std::nullopt,
			.type = PhysicalDeviceInfo::Type::Unknown
		};

		return info;

	}

	Backend::LogicalDevice Backend::CreateLogicalDevice(Backend::PhysicalDevice const& phys_dev) noexcept {
		return {
			phys_dev,
			std::make_shared<PresentationCache>(),
			execution::CompletionService::Instance()
		};
	}

}
#endif // !defined(__APPLE__)
