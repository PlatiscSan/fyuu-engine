module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <vector>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:instance_dispatch;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :physical_device;

namespace fyuu_rhi {

	template <class NativeInstance>
	struct ShareContextOnCurrentThread {
		NativeInstance const* instance;

		void operator()() const noexcept {
		}
	};

	template <class NativeInstance>
	struct EnumeratePhysicalDevices {
		NativeInstance const* instance;

		std::vector<PhysicalDevice> operator()() const {
			return {};
		}
	};

} // namespace fyuu_rhi
