module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:sampler_factory;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :sampler;
#if defined(_WIN32)
import :d3d12_data;
#endif // defined(_WIN32)
#if defined(__APPLE__)
import :metal_data;
#endif // defined(__APPLE__)
#if !defined(__APPLE__)
import :opengl_data;
import :vulkan_data;
#endif // !defined(__APPLE__)
import :webgpu_data;

namespace fyuu_rhi {

	struct SamplerImplementation {
		std::variant<
			std::monostate,
#if defined(_WIN32)
			d3d12::Sampler,
#endif // defined(_WIN32)
#if defined(__APPLE__)
			metal::Sampler,
#else
			vulkan::Sampler,
			opengl::Sampler,
#endif // defined(__APPLE__)
			webgpu::Sampler
		> native;
	};

	template <class NativeSampler>
	Sampler MakeSampler(NativeSampler&& native) {
		return Sampler(
			Sampler::UniqueHandle(
				new SamplerImplementation{ std::forward<NativeSampler>(native) },
				[](SamplerImplementation* implementation) noexcept {
					delete implementation;
				}
			)
		);
	}

} // namespace fyuu_rhi
