module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
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
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
import :d3d12_pipeline_resource_group_dispatch;
#endif // defined(_WIN32)
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :metal_pipeline_resource_group_dispatch;
#elif !defined(__EMSCRIPTEN__)
import :opengl_pipeline_resource_group_dispatch;
import :vulkan_pipeline_resource_group_dispatch;
#endif // !defined(__APPLE__)
import :webgpu_pipeline_resource_group_dispatch;

namespace fyuu_rhi {

	PipelineResourceGroup::PipelineResourceGroup() noexcept = default;
	PipelineResourceGroup::PipelineResourceGroup(ImplementationStorage&& impl) noexcept : m_impl(std::move(impl)) {
	}
	PipelineResourceGroup::PipelineResourceGroup(PipelineResourceGroup&& other) noexcept = default;
	PipelineResourceGroup& PipelineResourceGroup::operator=(PipelineResourceGroup&& other) noexcept = default;
	PipelineResourceGroup::~PipelineResourceGroup() noexcept = default;

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
