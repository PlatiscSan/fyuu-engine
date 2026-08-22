module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:logical_device_factory;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :logical_device;
#if defined(__APPLE__)
import :metal_data;
#endif // defined(__APPLE__)
#if defined(_WIN32)
import :d3d12_data;
#endif // defined(_WIN32)
#if !defined(__APPLE__)
import :opengl_data;
import :vulkan_data;
#endif // !defined(__APPLE__)
import :webgpu_data;

namespace fyuu_rhi {

	struct LogicalDeviceImplementation {
		std::variant<
			std::monostate,
#if defined(_WIN32)
			d3d12::LogicalDevice,
#endif // defined(_WIN32)
#if defined(__APPLE__)
			metal::LogicalDevice,
#else
			vulkan::LogicalDevice,
			opengl::LogicalDevice,
#endif // !defined(__APPLE__)
			webgpu::LogicalDevice
		> native;
	};

	template <class NativeLogicalDevice>
	LogicalDevice MakeLogicalDevice(NativeLogicalDevice&& native) {
		return LogicalDevice(
			LogicalDevice::UniqueHandle(
				new LogicalDeviceImplementation{ std::forward<NativeLogicalDevice>(native) },
				[](LogicalDeviceImplementation* implementation) noexcept {
					delete implementation;
				}
			)
		);
	}

#if !defined(__APPLE__)
	template <>
	LogicalDevice MakeLogicalDevice(opengl::LogicalDevice&& native) {
		static LogicalDeviceImplementation implementation{ std::move(native) };
		return LogicalDevice(
			LogicalDevice::UniqueHandle(
				&implementation,
				[](LogicalDeviceImplementation*) noexcept {
				}
			)
		);
	}
#endif // !defined(__APPLE__)

	template <>
	LogicalDevice MakeLogicalDevice(webgpu::LogicalDevice&& native) {
		static LogicalDeviceImplementation implementation{ std::move(native) };
		return LogicalDevice(
			LogicalDevice::UniqueHandle(
				&implementation,
				[](LogicalDeviceImplementation*) noexcept {
				}
			)
		);
	}

} // namespace fyuu_rhi
