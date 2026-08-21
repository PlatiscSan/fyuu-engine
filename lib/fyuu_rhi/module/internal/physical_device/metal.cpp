module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>

#include <optional>
#endif // !defined(__cpp_lib_modules)
#if defined(__APPLE__)
#include <Metal/Metal.hpp>
#endif // defined(__APPLE__)

module fyuu_rhi:metal_physical_device;
#if defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :metal_data;
import :logical_device_factory;
import :physical_device_dispatch;

namespace fyuu_rhi {

	template <>
	struct GetPhysicalDeviceInfo<metal::PhysicalDevice> {
		metal::PhysicalDevice const* physical_device;

		PhysicalDevice::Info operator()() const {
			auto name = physical_device->impl->name();
			return {
				.name = name ? name->utf8String() : "Metal device",
				.vendor_id = std::nullopt,
				.device_id = std::nullopt,
				.dedicated_memory = physical_device->impl->recommendedMaxWorkingSetSize(),
				.type = physical_device->impl->isLowPower() ?
					PhysicalDevice::Info::Type::IntegratedGPU :
					PhysicalDevice::Info::Type::DiscreteGPU
			};
		}
	};

	template <>
	struct CreateLogicalDevice<metal::PhysicalDevice> {
		metal::PhysicalDevice* physical_device;

		LogicalDevice operator()() const {
			// Metal has no separate device-creation step: the MTL::Device is
			// already obtained at enumeration and is both the physical and the
			// logical device. Command queues are created on demand by the
			// execution path, exactly like D3D12 does not allocate a command
			// queue here either.
			return MakeLogicalDevice(
				metal::LogicalDevice{ physical_device->impl }
			);
		}
	};

} // namespace fyuu_rhi
#endif // defined(__APPLE__)
