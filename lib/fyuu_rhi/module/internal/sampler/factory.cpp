module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>
#include <variant>
#endif // !defined(__cpp_lib_modules)

#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#include <boost/scope/unique_resource.hpp>
#endif // !defined(__APPLE__) && !defined(__EMSCRIPTEN__)

module fyuu_rhi:sampler_factory;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :sampler;
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
import :d3d12_data;
#endif // defined(_WIN32)
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :metal_data;
#endif // defined(__APPLE__)
#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :opengl_data;
import :vulkan_data;
#endif // !defined(__APPLE__)
import :webgpu_data;

namespace fyuu_rhi {

	struct SamplerImplementation {
		std::variant<
			std::monostate,
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
			d3d12::Sampler,
#endif // defined(_WIN32)
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
			metal::Sampler,
#elif !defined(__EMSCRIPTEN__)
			vulkan::Sampler,
			opengl::Sampler,
#endif // defined(__APPLE__)
			webgpu::Sampler
		> native;
	};

	/** @brief Emits inline-storage lifecycle operations where native template definitions are available. */
	template class details::FastPimpl<SamplerImplementation, 64u>;

	template <class NativeSampler>
	Sampler MakeSampler(NativeSampler&& native) {
		return Sampler(
			Sampler::ImplementationStorage(
				std::in_place, std::forward<NativeSampler>(native)
			)
		);
	}

} // namespace fyuu_rhi
