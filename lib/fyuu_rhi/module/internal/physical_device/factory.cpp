module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>
#include <variant>
#endif // !defined(__cpp_lib_modules)

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <D3D12MemAlloc.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#endif // defined(_WIN32) && !defined(__EMSCRIPTEN__)

module fyuu_rhi:physical_device_factory;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :physical_device;
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :metal_data;
#endif // defined(__APPLE__)
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
import :d3d12_data;
#endif // defined(_WIN32)
#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :opengl_data;
import :vulkan_data;
#endif // !defined(__APPLE__)
import :webgpu_data;

namespace fyuu_rhi {

	struct PhysicalDeviceImplementation {
		std::variant<
			std::monostate,
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
			d3d12::PhysicalDevice,
#endif // defined(_WIN32)
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
			metal::PhysicalDevice,
#elif !defined(__EMSCRIPTEN__)
			vulkan::PhysicalDevice,
			opengl::PhysicalDevice,
#endif // defined(__APPLE__)
			webgpu::PhysicalDevice
		> native;
	};

	/** @brief Emits inline-storage lifecycle operations where native template definitions are available. */
	template class details::FastPimpl<PhysicalDeviceImplementation, 96u>;

	template <class NativePhysicalDevice>
	PhysicalDevice MakePhysicalDevice(NativePhysicalDevice&& native) {
		return PhysicalDevice(
			PhysicalDevice::ImplementationStorage(
				std::in_place, std::forward<NativePhysicalDevice>(native)
			)
		);
	}

} // namespace fyuu_rhi
