module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:physical_device_dispatch;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :physical_device;

namespace fyuu_rhi {

	template <class NativePhysicalDevice>
	struct GetPhysicalDeviceInfo {
		NativePhysicalDevice const* physical_device;

		PhysicalDevice::Info operator()() const {
			return {};
		}
	};

	template <class NativePhysicalDevice>
	struct CreateLogicalDevice {
		NativePhysicalDevice* physical_device;

		LogicalDevice operator()() const {
			throw std::runtime_error(
				"Logical device creation is not implemented for this backend"
			);
		}
	};

} // namespace fyuu_rhi
