module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <utility>
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :physical_device;
import :physical_device_dispatch;
import :physical_device_factory;
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :metal_physical_device;
#endif // defined(__APPLE__)
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
import :d3d12_physical_device;
#endif // defined(_WIN32)
#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :opengl_physical_device;
import :vulkan_physical_device;
#endif // !defined(__APPLE__)
import :webgpu_physical_device;

namespace fyuu_rhi {

	PhysicalDevice::PhysicalDevice() noexcept = default;
	PhysicalDevice::PhysicalDevice(ImplementationStorage&& impl) noexcept : m_impl(std::move(impl)) {
	}
	PhysicalDevice::PhysicalDevice(PhysicalDevice&& other) noexcept = default;
	PhysicalDevice& PhysicalDevice::operator=(PhysicalDevice&& other) noexcept = default;
	PhysicalDevice::~PhysicalDevice() noexcept = default;

	PhysicalDevice::Info PhysicalDevice::GetInfo() const {
		if (!m_impl) {
			return {};
		}
		return std::visit(
			[]<class NativePhysicalDevice>(NativePhysicalDevice const& native) {
				return GetPhysicalDeviceInfo<NativePhysicalDevice>{ &native }();
			},
			m_impl->native
		);
	}

	LogicalDevice PhysicalDevice::CreateLogicalDevice() const {
		if (!m_impl) {
			throw std::runtime_error("Cannot create a logical device from an empty physical device");
		}
		return std::visit(
			[]<class NativePhysicalDevice>(NativePhysicalDevice const& native) {
				return fyuu_rhi::CreateLogicalDevice<NativePhysicalDevice>{ &native }();
			},
			m_impl->native
		);
	}

} // namespace fyuu_rhi
