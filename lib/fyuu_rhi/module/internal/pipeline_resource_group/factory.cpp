module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>
#include <variant>
#endif // !defined(__cpp_lib_modules)


#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <d3d12.h>
#include <wrl.h>
#endif // defined(_WIN32) && !defined(__EMSCRIPTEN__)

module fyuu_rhi:pipeline_resource_group_factory;
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

	struct PipelineResourceGroupImplementation {
		std::variant<
			std::monostate,
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
			d3d12::PipelineResourceGroup,
#endif // defined(_WIN32)
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
			metal::PipelineResourceGroup,
#elif !defined(__EMSCRIPTEN__)
			vulkan::PipelineResourceGroup,
			opengl::PipelineResourceGroup,
#endif // !defined(__APPLE__)
			webgpu::PipelineResourceGroup
		> native;
	};

	/** @brief Emits inline-storage lifecycle operations where native template definitions are available. */
	template class details::FastPimpl<PipelineResourceGroupImplementation, 160u>;

	template <class NativePipelineResourceGroup>
	PipelineResourceGroup MakePipelineResourceGroup(NativePipelineResourceGroup&& native) {
		return PipelineResourceGroup(
			PipelineResourceGroup::ImplementationStorage(
				std::in_place, std::forward<NativePipelineResourceGroup>(native)
			)
		);
	}

} // namespace fyuu_rhi
