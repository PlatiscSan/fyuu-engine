module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:pipeline_resource_group_factory;
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

	struct PipelineResourceGroupImplementation {
		std::variant<
			std::monostate,
#if defined(_WIN32)
			d3d12::PipelineResourceGroup,
#endif // defined(_WIN32)
#if defined(__APPLE__)
			metal::PipelineResourceGroup,
#else
			vulkan::PipelineResourceGroup,
			opengl::PipelineResourceGroup,
#endif // !defined(__APPLE__)
			webgpu::PipelineResourceGroup
		> native;
	};

	template <class NativePipelineResourceGroup>
	PipelineResourceGroup MakePipelineResourceGroup(NativePipelineResourceGroup&& native) {
		return PipelineResourceGroup(
			PipelineResourceGroup::UniqueHandle(
				new PipelineResourceGroupImplementation{ std::forward<NativePipelineResourceGroup>(native) },
				[](PipelineResourceGroupImplementation* implementation) noexcept {
					delete implementation;
				}
			)
		);
	}

} // namespace fyuu_rhi
