module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:native_command_graph;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :command_graph;
import :command_graph_plan;

namespace fyuu_rhi::execution {
	struct GraphCompletionValue {
		GraphCompletion completion;

		void operator()() const noexcept {
			completion.SetValue(completion.operation);
		}
	};

	template <class Backend> struct NativeCommandGraph {
		CommandGraphDescriptor descriptor;
		NativeCommandGraphBindings<Backend> bindings;
	};

	template <class Backend> struct NativeExecutableGraph {
		std::shared_ptr<NativeCommandGraph<Backend> const> impl;
		CommandGraphPlan plan;
	};

	template <class Backend> std::shared_ptr<NativeCommandGraph<Backend>> MakeCommandGraph(
		CommandGraphDescriptor const& descriptor,
		NativeCommandGraphBindings<Backend> const& bindings
	) {
		return std::make_shared<NativeCommandGraph<Backend>>(descriptor, bindings);
	}

	template <class Backend> std::shared_ptr<NativeExecutableGraph<Backend>> MakeExecutableGraph(
		std::shared_ptr<NativeCommandGraph<Backend>> const& graph
	) {
		return std::make_shared<NativeExecutableGraph<Backend>>(
			graph,
			CompileCommandGraphPlan(graph->descriptor)
		);
	}

}
