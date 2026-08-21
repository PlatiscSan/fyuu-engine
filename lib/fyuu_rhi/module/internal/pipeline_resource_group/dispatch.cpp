module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:pipeline_resource_group_dispatch;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :pipeline;

namespace fyuu_rhi {

	template <class NativePipelineResourceGroup>
	struct GetPipelineResourceGroupSpace {
		NativePipelineResourceGroup const* resource_group;

		std::uint32_t operator()() const noexcept {
			return 0u;
		}
	};
} // namespace fyuu_rhi
