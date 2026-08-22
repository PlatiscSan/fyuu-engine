module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:pipeline_factory;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :pipeline;
#if defined(_WIN32)
import :d3d12_data;
#endif // defined(_WIN32)
#if !defined(__APPLE__)
import :opengl_data;
import :vulkan_data;
#endif // !defined(__APPLE__)
import :webgpu_data;

namespace fyuu_rhi {

	struct PipelineImplementation {
		std::variant<
			std::monostate,
#if defined(_WIN32)
			d3d12::Pipeline,
#endif // defined(_WIN32)
#if defined(__APPLE__)
			metal::Pipeline,
#else
			vulkan::Pipeline,
			opengl::Pipeline,
#endif // !defined(__APPLE__)
			webgpu::Pipeline
		> native;
	};

	template <class NativePipeline>
	Pipeline MakePipeline(NativePipeline&& native) {
		return Pipeline(
			Pipeline::UniqueHandle(
				new PipelineImplementation{ std::forward<NativePipeline>(native) },
				[](PipelineImplementation* implementation) noexcept {
					delete implementation;
				}
			)
		);
	}

} // namespace fyuu_rhi
