module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:command_scheduler_factory;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :execution;
#if defined(__APPLE__)
import :metal_data;
#endif // defined(__APPLE__)
#if defined(_WIN32)
import :d3d12_data;
#endif // defined(_WIN32)
#if !defined(__APPLE__)
import :opengl_data;
import :vulkan_data;
#endif // !defined(__APPLE__)
import :webgpu_data;

namespace fyuu_rhi::execution {

	struct CommandSchedulerContext {
		std::variant<
			std::monostate,
#if defined(_WIN32)
			d3d12::CommandSchedulerContext,
#endif // defined(_WIN32)
#if defined(__APPLE__)
			metal::CommandSchedulerContext,
#else
			vulkan::CommandSchedulerContext,
			opengl::CommandSchedulerContext,
#endif // !defined(__APPLE__)
			webgpu::CommandSchedulerContext
		> native;
	};

	template <class NativeCommandSchedulerContext>
	CommandScheduler MakeCommandScheduler(NativeCommandSchedulerContext&& native) {
		return CommandScheduler(
			std::make_shared<CommandSchedulerContext>(
				std::forward<NativeCommandSchedulerContext>(native)
			)
		);
	}

} // namespace fyuu_rhi::execution
