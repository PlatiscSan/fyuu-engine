module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :command_scheduler_dispatch;
import :command_scheduler_factory;
import :execution;
import :webgpu_command_scheduler;
#if defined(__APPLE__)
import :metal_command_scheduler;
#endif // defined(__APPLE__)
#if defined(_WIN32)
import :d3d12_command_scheduler;
#endif // defined(_WIN32)
#if !defined(__APPLE__)
import :opengl_command_scheduler;
import :vulkan_data;
import :vulkan_command_scheduler;
#endif // !defined(__APPLE__)

namespace fyuu_rhi::execution {

	CompletionToken CommandScheduler::Execute(
		ExecutionPlan const& plan,
		std::span<PlatformHandle const> presentation_targets,
		std::span<Resource const> resources,
		std::span<View const> views,
		std::span<Sampler const> samplers,
		std::span<Pipeline const> pipelines,
		std::span<PipelineResourceGroup const> resource_groups,
		StopTokenView stop_token
	) {
		if (!m_impl) {
			throw std::runtime_error("Cannot execute commands with an empty scheduler");
		}
		return std::visit(
			[&]<class NativeCommandSchedulerContext>(
				NativeCommandSchedulerContext& native
			) {
#if !defined(__APPLE__)
				if constexpr (std::same_as<
					NativeCommandSchedulerContext,
					vulkan::CommandSchedulerContext
				>) {
					return ExecuteCommands<NativeCommandSchedulerContext>{
						&native,
						m_impl
					}(
						plan,
						presentation_targets,
						resources,
						views,
						samplers,
						pipelines,
						resource_groups,
						stop_token
					);
				}
				else {
#endif // !defined(__APPLE__)
					return ExecuteCommands<NativeCommandSchedulerContext>{ &native }(
					plan,
					presentation_targets,
					resources,
					views,
					samplers,
					pipelines,
					resource_groups,
					stop_token
					);
#if !defined(__APPLE__)
				}
#endif // !defined(__APPLE__)
			},
			m_impl->native
		);
	}

} // namespace fyuu_rhi::execution
