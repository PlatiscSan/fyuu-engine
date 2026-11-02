module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :sampler_factory;
import :sampler;

namespace fyuu_rhi {

	Sampler::Sampler() noexcept = default;
	Sampler::Sampler(ImplementationStorage&& impl) noexcept : m_impl(std::move(impl)) {
	}
	Sampler::Sampler(Sampler&& other) noexcept = default;
	Sampler& Sampler::operator=(Sampler&& other) noexcept = default;
	Sampler::~Sampler() noexcept = default;

} // namespace fyuu_rhi
