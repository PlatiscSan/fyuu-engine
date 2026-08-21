module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <span>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:command_scheduler_dispatch;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :execution;

namespace fyuu_rhi::execution {

	template <class NativeCommandSchedulerContext>
	struct ExecuteCommands {
		NativeCommandSchedulerContext* context;

		CompletionToken operator()(
			ExecutionPlan const&,
			std::span<PlatformHandle const>,
			std::span<Resource const>,
			std::span<View const>,
			std::span<Sampler const>,
			std::span<Pipeline const>,
			std::span<PipelineResourceGroup const>,
			StopTokenView
		) const {
			(void)context;
			throw std::runtime_error(
				"Command execution is not implemented for this backend"
			);
		}
	};

} // namespace fyuu_rhi::execution
