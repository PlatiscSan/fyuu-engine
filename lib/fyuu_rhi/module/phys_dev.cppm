module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <concepts>
#endif // !defined(__cpp_lib_modules)
export module fyuu_rhi:physical_device;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core_types;
import :logical_device;

namespace fyuu_rhi {
	export template <class Backend> class PhysicalDevice {
	public:
		using Implementation = typename Backend::PhysicalDevice;

	private:
		Implementation m_impl;

	public:
		template <std::convertible_to<Implementation> I>
		PhysicalDevice(I&& impl)
			: m_impl(std::forward<I>(impl)) {

		}

		PhysicalDeviceInfo GetInfo() const {
			using Ret = decltype(Backend::GetPhysicalDeviceInfo(m_impl));
			static_assert(std::same_as<Ret, PhysicalDeviceInfo>, "Backend::GetPhysicalDeviceInfo() must return PhysicalDeviceInfo");
			return Backend::GetPhysicalDeviceInfo(m_impl);
		}

		LogicalDevice<Backend> CreateLogicalDevice() {
			using Ret = decltype(Backend::CreateLogicalDevice(m_impl));
			static_assert(std::constructible_from<LogicalDevice<Backend>, Ret>,
				"LogicalDevice<Backend> must be constructible from logical device returned by CreateLogicalDevice()");
			return Backend::CreateLogicalDevice(m_impl);
		}

	};
}