module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <Metal/Metal.hpp>
#endif // defined(__APPLE__)

module fyuu_rhi:metal_instance;
#if defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :instance_dispatch;
import :instance_factory;
import :metal_data;
import :physical_device_factory;

namespace fyuu_rhi {

	template <>
	struct CreateInstance<metal::Instance> {
		metal::Instance operator()() const noexcept {
			return {};
		}
	};

	template <>
	struct EnumeratePhysicalDevices<metal::Instance> {
		metal::Instance const* instance;

		std::vector<PhysicalDevice> operator()() const {
			std::vector<PhysicalDevice> physical_devices;
#if TARGET_OS_OSX
			auto devices = NS::TransferPtr(MTL::CopyAllDevices());
			physical_devices.reserve(devices->count());
			for (NS::UInteger index = 0u; index < devices->count(); ++index) {
				auto device = devices->object<MTL::Device>(index);
				physical_devices.emplace_back(MakePhysicalDevice(metal::PhysicalDevice{ NS::RetainPtr(device) }));
			}
#else
			auto device = NS::TransferPtr(MTL::CreateSystemDefaultDevice());
			if (device) {
				physical_devices.emplace_back(
					MakePhysicalDevice(
						metal::PhysicalDevice{ std::move(device) }
					)
				);
			}
#endif // TARGET_OS_OSX
			if (physical_devices.empty()) {
				throw std::runtime_error("Metal did not report an available physical device");
			}
			return physical_devices;
		}
	};

} // namespace fyuu_rhi
#endif // defined(__APPLE__)
