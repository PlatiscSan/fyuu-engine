module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <variant>

#include <span>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :pipeline;
import :pipeline_dispatch;
import :pipeline_factory;
#if defined(_WIN32)
import :d3d12_pipeline;
#endif // defined(_WIN32)
#if defined(__APPLE__)
import :metal_pipeline;
#else
import :opengl_pipeline;
import :vulkan_pipeline;
#endif // !defined(__APPLE__)
import :webgpu_pipeline;

namespace fyuu_rhi {

	PipelineResourceGroup Pipeline::CreatePipelineResourceGroup(std::uint32_t space, std::span<pipeline::ResourceBinding const> bindings) {
		if (!m_impl) {
			throw std::runtime_error(
				"Cannot create a resource group from an empty pipeline"
			);
		}
		return std::visit(
			[&]<class NativePipeline>(NativePipeline& native) {
				return fyuu_rhi::CreatePipelineResourceGroup<NativePipeline>{ &native }(
					space,
					bindings
				);
			},
			m_impl->native
		);
	}

} // namespace fyuu_rhi
