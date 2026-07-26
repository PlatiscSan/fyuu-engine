module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <concepts>
#endif // !defined(__cpp_lib_modules)

export module fyuu_rhi:sampler;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
namespace fyuu_rhi {

	export template <class Backend> class Sampler {
	public:
		using Implementation = typename Backend::Sampler;
	private:
		Implementation m_impl;
	public:
		template <class SamplerType>
		class PassKey {
			static_assert(
				std::same_as<SamplerType, Sampler> || std::same_as<SamplerType, Sampler const>,
				"SamplerType must be Sampler or const Sampler"
			);

			template <class U> friend class LogicalDevice;

			SamplerType* m_sampler;

			template <class Self>
			decltype(auto) GetImplementation(this Self&& self) noexcept {
				return (self.m_sampler->m_impl);
			}

		public:
			explicit PassKey(SamplerType* sampler) noexcept
				: m_sampler(sampler) {

			}
		};

		template <std::convertible_to<Implementation> I>
		Sampler(I&& impl)
			: m_impl(std::forward<I>(impl)) {

		}

		Sampler(Sampler const&) = delete;
		Sampler& operator=(Sampler const&) = delete;
		Sampler(Sampler&&) noexcept = default;
		Sampler& operator=(Sampler&&) noexcept = default;
		~Sampler() noexcept = default;

		template <class Self>
		auto GetPassKey(this Self&& self) noexcept {
			using SamplerType = std::remove_reference_t<Self>;
			return PassKey<SamplerType>{ &self };
		}
	};

}
