module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <vector>
#include <functional>
#include <cstdint>
#include <variant>
#include <span>
#endif // !defined(__cpp_lib_modules)
#if defined(__ANDROID__)
#include <android/native_window.h>
#endif // defined(__ANDROID__)
export module fyuu_rhi:instance;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core;
import :physical_device;

export namespace fyuu_rhi {

	class Instance {
	private:
		struct InstanceImplementation* m_impl;

	public:
		Instance(struct InstanceImplementation* impl) noexcept
			: m_impl(impl) {

		}
		
		explicit operator bool() const noexcept {
			return static_cast<bool>(m_impl);
		}

		void ShareContextOnThisThread();

		std::vector<PhysicalDevice> EnumeratePhysicalDevices() const;
	};

	std::span<Backend const> EnumerateBackends() noexcept;

	void RequestInstance(Backend backend, std::function<void(Instance)> const& func);
	
} // namespace fyuu_rhi
