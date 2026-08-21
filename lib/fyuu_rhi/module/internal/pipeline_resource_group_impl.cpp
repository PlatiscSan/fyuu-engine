module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource;
import :pipeline;
import :pipeline_resource_group_dispatch;
import :pipeline_resource_group_factory;
#if defined(_WIN32)
import :d3d12_pipeline_resource_group_dispatch;
#endif // defined(_WIN32)
#if defined(__APPLE__)
import :metal_pipeline_resource_group_dispatch;
#else
import :opengl_pipeline_resource_group_dispatch;
import :vulkan_pipeline_resource_group_dispatch;
#endif // !defined(__APPLE__)
import :webgpu_pipeline_resource_group_dispatch;

namespace fyuu_rhi {

	std::uint32_t PipelineResourceGroup::Space() const noexcept {
		if (!m_impl) {
			return 0u;
		}
		return std::visit(
			[]<class NativePipelineResourceGroup>(NativePipelineResourceGroup const& native) noexcept {
				return GetPipelineResourceGroupSpace<NativePipelineResourceGroup>{ &native }();
			},
			m_impl->native
		);
	}

} // namespace fyuu_rhi
