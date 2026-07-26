module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <stdexcept>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:native_command_graph;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :command_graph;
import :command_graph_plan;

namespace fyuu_rhi::execution {
	template <class Backend> struct NativeCommandGraph {
		CommandGraphDescriptor descriptor;
		NativeCommandGraphBindings<Backend> bindings;
	};

	template <class Backend> struct NativeExecutableGraph {
		std::shared_ptr<NativeCommandGraph<Backend> const> impl;
		CommandGraphPlan plan;
	};

	template <class Backend> std::shared_ptr<NativeCommandGraph<Backend>> MakeCommandGraph(
		CommandGraphDescriptor const& descriptor
	) {
		return std::make_shared<NativeCommandGraph<Backend>>(
			descriptor,
			NativeCommandGraphBindings<Backend>{}
		);
	}

	template <class Backend> std::shared_ptr<NativeExecutableGraph<Backend>> BindExecutableGraph(
		std::shared_ptr<NativeExecutableGraph<Backend>> const& graph,
		NativeCommandGraphBindings<Backend> const& bindings
	) {
		auto const& descriptor = graph->impl->descriptor;
		if (bindings.resources.size() != descriptor.resource_count ||
			bindings.pipelines.size() != descriptor.pipeline_count ||
			bindings.views.size() != descriptor.view_count ||
			bindings.resource_groups.size() != descriptor.resource_group_count ||
			bindings.presentation_targets.size() != descriptor.presentation_target_count) {
			throw std::invalid_argument(
				"BindExecutableGraph(): binding counts do not match the executable graph"
			);
		}
		auto bound_graph = std::make_shared<NativeCommandGraph<Backend>>(
			descriptor,
			bindings
		);
		return std::make_shared<NativeExecutableGraph<Backend>>(bound_graph, graph->plan);
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
