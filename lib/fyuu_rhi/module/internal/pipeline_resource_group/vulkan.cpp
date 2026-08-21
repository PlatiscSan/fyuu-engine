module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:vulkan_pipeline_resource_group_dispatch;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :pipeline_resource_group_dispatch;
import :vulkan_data;

namespace fyuu_rhi {

	template <>
	struct GetPipelineResourceGroupSpace<vulkan::PipelineResourceGroup> {
		vulkan::PipelineResourceGroup const* resource_group;

		std::uint32_t operator()() const noexcept {
			return resource_group->space;
		}
	};

} // namespace fyuu_rhi
