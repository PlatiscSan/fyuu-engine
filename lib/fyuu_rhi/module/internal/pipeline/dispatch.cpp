module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>

#include <cstdint>

#include <span>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:pipeline_dispatch;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :pipeline;

namespace fyuu_rhi {

	template <class NativePipeline>
	struct CreatePipelineResourceGroup {
		NativePipeline* native;

		PipelineResourceGroup operator()(std::uint32_t, std::span<pipeline::ResourceBinding const>) const {
			throw std::runtime_error(
				"Pipeline resource group creation is not implemented for this backend"
			);
		}
	};

} // namespace fyuu_rhi
