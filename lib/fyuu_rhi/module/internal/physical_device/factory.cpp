module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:physical_device_factory;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :physical_device;
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

	struct PhysicalDeviceImplementation {
		std::variant<
			std::monostate,
#if defined(_WIN32)
			d3d12::PhysicalDevice,
#endif // defined(_WIN32)
#if defined(__APPLE__)
			metal::PhysicalDevice,
#else
			vulkan::PhysicalDevice,
			opengl::PhysicalDevice,
#endif // defined(__APPLE__)
			webgpu::PhysicalDevice
		> native;
	};

	namespace {
#if !defined(__APPLE__)
		PhysicalDeviceImplementation s_opengl_physical_device;
#endif // !defined(__APPLE__)
	} // namespace

	template <class NativePhysicalDevice>
	PhysicalDevice MakePhysicalDevice(NativePhysicalDevice&& native) {
		return PhysicalDevice(
			PhysicalDevice::UniqueHandle(
				new PhysicalDeviceImplementation{ std::forward<NativePhysicalDevice>(native) },
				[](PhysicalDeviceImplementation* implementation) noexcept {
					delete implementation;
				}
			)
		);
	}

#if !defined(__APPLE__)
	template <>
	PhysicalDevice MakePhysicalDevice(opengl::PhysicalDevice&& native) {
		if (std::holds_alternative<std::monostate>(s_opengl_physical_device.native)) {
			s_opengl_physical_device.native.emplace<opengl::PhysicalDevice>(std::move(native));
		}
		return PhysicalDevice(
			PhysicalDevice::UniqueHandle(
				&s_opengl_physical_device,
				[](PhysicalDeviceImplementation*) noexcept {
				}
			)
		);
	}
#endif // !defined(__APPLE__)

} // namespace fyuu_rhi
