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
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <D3D12MemAlloc.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#endif // defined(_WIN32) && !defined(__EMSCRIPTEN__)

module fyuu_rhi:pipeline_factory;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :pipeline;
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :metal_data;
#endif // defined(__APPLE__)
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
import :d3d12_data;
#endif // defined(_WIN32)
#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :opengl_data;
import :vulkan_data;
#endif // !defined(__APPLE__)
import :webgpu_data;

namespace fyuu_rhi {

	struct PipelineImplementation {
		std::variant<
			std::monostate,
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
			d3d12::Pipeline,
#endif // defined(_WIN32)
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
			metal::Pipeline,
#elif !defined(__EMSCRIPTEN__)
			vulkan::Pipeline,
			opengl::Pipeline,
#endif // !defined(__APPLE__)
			webgpu::Pipeline
		> native;
	};

	/** @brief Emits inline-storage lifecycle operations where native template definitions are available. */
	template class details::FastPimpl<PipelineImplementation, 320u>;

	template <class NativePipeline>
	Pipeline MakePipeline(NativePipeline&& native) {
		return Pipeline(
			Pipeline::ImplementationStorage(
				std::in_place, std::forward<NativePipeline>(native)
			)
		);
	}

} // namespace fyuu_rhi
