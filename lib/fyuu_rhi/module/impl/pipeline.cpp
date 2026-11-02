module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <utility>
#include <vector>

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
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
import :d3d12_pipeline;
#endif // defined(_WIN32)
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :metal_pipeline;
#elif !defined(__EMSCRIPTEN__)
import :opengl_pipeline;
import :vulkan_pipeline;
#endif // !defined(__APPLE__)
import :webgpu_pipeline;

namespace fyuu_rhi {

	Pipeline::Pipeline() noexcept = default;
	Pipeline::Pipeline(ImplementationStorage&& impl) noexcept : m_impl(std::move(impl)) {
	}
	Pipeline::Pipeline(Pipeline&& other) noexcept = default;
	Pipeline& Pipeline::operator=(Pipeline&& other) noexcept = default;
	Pipeline::~Pipeline() noexcept = default;

	std::vector<pipeline::BindingMetadata> Pipeline::Bindings() const {
		if (!m_impl) {
			throw std::runtime_error("Cannot reflect the bindings of an empty pipeline");
		}
		return std::visit(
			[&]<class NativePipeline>(NativePipeline& native) {
				return fyuu_rhi::PipelineBindings<NativePipeline>{ &native }();
			},
			m_impl->native
		);
	}

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
