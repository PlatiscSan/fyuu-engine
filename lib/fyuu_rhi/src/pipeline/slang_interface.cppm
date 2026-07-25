module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#include <string>
#include <vector>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:slang_pipeline_interface;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :pipeline_types;
import :resource_types;

namespace fyuu_rhi::pipeline {

	// Normalized pipeline reflection shared by all backends. This partition is
	// intentionally not exported: resource bindings remain a pipeline detail.
	struct SlangPipelineBinding {
		std::string name;
		ResourceFlags flags;
		std::uint32_t slot = 0;
		std::uint32_t space = 0;
		std::uint32_t count = 1;
		std::uint32_t visibility = 0;
	};

	struct SlangPipelineVertexInput {
		std::string name;
		std::uint32_t location = 0;
		std::string semantic_name;
		std::uint32_t semantic_index = 0;
	};

	struct SlangPipelineFragmentOutput {
		std::string name;
		std::uint32_t location = 0;
	};

	struct SlangPipelinePushConstantRange {
		std::uint32_t offset = 0;
		std::uint32_t size = 0;
		std::uint32_t slot = 0;
		std::uint32_t space = 0;
		std::uint32_t visibility = 0;
	};

	struct SlangPipelineInterface {
		std::vector<SlangPipelineBinding> bindings;
		std::vector<SlangPipelineVertexInput> vertex_inputs;
		std::vector<SlangPipelineFragmentOutput> fragment_outputs;
		std::vector<SlangPipelinePushConstantRange> push_constants;
	};

}
