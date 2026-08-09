module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>

#include <vector>

#include <type_traits>

#include <mutex>

#include <ranges>
#include <iterator>
#endif // !defined(__cpp_lib_modules)
export module fyuu_rhi:instance;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :physical_device;

namespace fyuu_rhi {

	namespace details {

		template <class T> struct IsVector : public std::false_type {};

		template <class T, class Alloc> struct IsVector<std::vector<T, Alloc>> : public std::true_type {};

	}

	export template <class Backend> class Instance {
	private:
		typename Backend::Instance m_impl;
		static inline Instance* s_instance;

		template <class... Args>
		Instance(std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver, Args&&... args)
			: m_impl(Backend::CreateInstance(app_name, app_ver, engine_name, engine_ver, std::forward<Args>(args)...)) {

		}

	public:
		template <class... Args>
		static void Initialize(std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver, Args&&... args) {
			static Instance instance(app_name, app_ver, engine_name, engine_ver, std::forward<Args>(args)...);
			static std::once_flag init_flag;
			auto SetInstance = [&]() { s_instance = &instance; };
			std::call_once(init_flag, SetInstance);
		}

		static Instance* Get() noexcept {
			return s_instance;
		}

		~Instance() noexcept {
			if constexpr (requires{ Backend::DestroyInstance(m_impl); }) {
				Backend::DestroyInstance(m_impl);
			}
		}

		auto EnumeratePhysicalDevices() const {

			using Ret = decltype(Backend::EnumeratePhysicalDevices(m_impl));
			using PhysDev = PhysicalDevice<Backend>;

			if constexpr (details::IsVector<Ret>::value) {
				using Vector = Ret;
				using Element = typename Vector::value_type;
				static_assert(std::constructible_from<PhysDev, Element>, 
					"PhysicalDevice<Backend> must be constructible from physical device returned by EnumeratePhysicalDevices()");

				auto devices = Backend::EnumeratePhysicalDevices(m_impl);
				std::vector<PhysDev> result;
				result.reserve(devices.size());
				for (auto&& dev : devices) {
					result.emplace_back(dev);
				}
				return result;

			}
			else {
				static_assert(std::constructible_from<PhysDev, Ret>, 
					"PhysicalDevice<Backend> must be constructible from physical device returned by EnumeratePhysicalDevices()");
				return PhysDev{ Backend::EnumeratePhysicalDevices(m_impl) };
			}

		}

		void ShareContextOnThisThread() {
			if constexpr (requires{ Backend::ShareContextOnThisThread(m_impl); }) {
				Backend::ShareContextOnThisThread(m_impl);
			}
		}

	};
	
} // namespace fyuu_rhi
