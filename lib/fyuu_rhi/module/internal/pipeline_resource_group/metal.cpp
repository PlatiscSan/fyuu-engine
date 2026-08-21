module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:metal_pipeline_resource_group_dispatch;
#if defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :metal_data;
import :pipeline_resource_group_dispatch;

namespace fyuu_rhi {

	template <>
	struct GetPipelineResourceGroupSpace<metal::PipelineResourceGroup> {
		metal::PipelineResourceGroup const* resource_group;

		std::uint32_t operator()() const noexcept {
			return resource_group->space;
		}
	};

} // namespace fyuu_rhi
#endif // defined(__APPLE__)
