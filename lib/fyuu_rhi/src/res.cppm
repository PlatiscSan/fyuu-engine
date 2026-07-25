module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <concepts>
#endif // !defined(__cpp_lib_modules)

export module fyuu_rhi:resource;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
namespace fyuu_rhi {

	export template <class Backend> class Resource {
	public:
		using Implementation = typename Backend::Resource;

	private:
		Implementation m_impl;

	public:
		template <class ResType>
		class LogicalDevicePassKey {
			static_assert(std::same_as<ResType, Resource> || std::same_as<ResType, Resource const>,
				"ResType must be Resource or const Resource");

			template <class U> friend class LogicalDevice;

			ResType* m_res;

			template <class Self>
			decltype(auto) GetImplementation(this Self&& self) noexcept {
				return self.m_res->m_impl;
			}

		public:
			explicit LogicalDevicePassKey(ResType* res) noexcept : m_res(res) {}
		};

		template <std::convertible_to<Implementation> I>
		Resource(I&& impl)
			: m_impl(std::forward<I>(impl)) {

		}

		Resource(Resource const&) = delete;
		Resource& operator=(Resource const&) = delete;
		Resource(Resource&&) noexcept = default;
		Resource& operator=(Resource&&) noexcept = default;
		~Resource() noexcept = default;

		template <class Self>
		auto GetLogicalDevicePassKey(this Self&& self) noexcept {
			using ResType = std::remove_reference_t<Self>;
			return LogicalDevicePassKey<ResType>{ &self };
		}

	};

}
