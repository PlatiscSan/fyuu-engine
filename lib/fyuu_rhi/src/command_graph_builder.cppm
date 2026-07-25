module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <queue>
#include <stdexcept>
#include <vector>
#endif // !defined(__cpp_lib_modules)

export module fyuu_rhi:command_graph_builder;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :command_graph_types;
import :command_graph_validation;

namespace fyuu_rhi::execution {

	export class CommandGraphBuilder {
	private:
		CommandGraphDescriptor m_descriptor;

		GraphNodeDescriptor& GetNode(GraphNodeID id) {
			if (id.value >= m_descriptor.nodes.size()) {
				throw std::out_of_range("CommandGraphBuilder: invalid graph node ID");
			}
			return m_descriptor.nodes[id.value];
		}

		GraphNodeDescriptor const& GetNode(GraphNodeID id) const {
			if (id.value >= m_descriptor.nodes.size()) {
				throw std::out_of_range("CommandGraphBuilder: invalid graph node ID");
			}
			return m_descriptor.nodes[id.value];
		}

		void ValidateResource(GraphResourceID id) const {
			if (id.value >= m_descriptor.resource_count) {
				throw std::out_of_range("CommandGraphBuilder: invalid graph resource ID");
			}
		}

	public:
		[[nodiscard]] GraphResourceID RegisterResource() noexcept {
			return GraphResourceID{ m_descriptor.resource_count++ };
		}

		[[nodiscard]] GraphPipelineID RegisterPipeline() noexcept {
			return GraphPipelineID{ m_descriptor.pipeline_count++ };
		}

		[[nodiscard]] GraphViewID RegisterView() noexcept {
			return GraphViewID{ m_descriptor.view_count++ };
		}

		[[nodiscard]] GraphResourceGroupID RegisterResourceGroup() noexcept {
			return GraphResourceGroupID{ m_descriptor.resource_group_count++ };
		}

		[[nodiscard]] GraphPresentationID RegisterPresentationTarget() noexcept {
			return GraphPresentationID{ m_descriptor.presentation_target_count++ };
		}

		[[nodiscard]] GraphNodeID AddNode(GraphNodeFlagBits flags) {
			if (flags == GraphNodeFlagBits::None) {
				throw std::invalid_argument("CommandGraphBuilder::AddNode(): node capability must not be empty");
			}
			auto value = static_cast<std::uint32_t>(m_descriptor.nodes.size());
			m_descriptor.nodes.push_back({
				.id = GraphNodeID{ value },
				.flags = flags
			});
			return GraphNodeID{ value };
		}

		void AddDependency(GraphNodeID node, GraphNodeID dependency) {
			if (node == dependency) {
				throw std::invalid_argument("CommandGraphBuilder::AddDependency(): a node cannot depend on itself");
			}
			GetNode(dependency);
			auto& dependencies = GetNode(node).dependencies;
			if (!std::ranges::contains(dependencies, dependency)) {
				dependencies.emplace_back(dependency);
			}
		}

		void AddAccess(GraphNodeID node, GraphResourceAccess const& access) {
			ValidateResource(access.resource);
			if (!HasGraphAccess(access.flags, GraphAccessFlagBits::Read) &&
				!HasGraphAccess(access.flags, GraphAccessFlagBits::Write)) {
				throw std::invalid_argument("CommandGraphBuilder::AddAccess(): access must include Read or Write");
			}

			auto& current = GetNode(node);
			for (auto const& previous : m_descriptor.nodes) {
				if (previous.id == node) {
					break;
				}
				for (auto const& previous_access : previous.accesses) {
					if (previous_access.resource != access.resource ||
						!GraphSubresourceRangesOverlap(previous_access.range, access.range)) {
						continue;
					}
					if (HasGraphAccess(previous_access.flags, GraphAccessFlagBits::Write) ||
						HasGraphAccess(access.flags, GraphAccessFlagBits::Write) ||
						previous.flags != current.flags) {
						AddDependency(node, previous.id);
					}
				}
			}
			current.accesses.emplace_back(access);
		}

		void AddCommand(GraphNodeID node, GraphCommand const& command) {
			GetNode(node).commands.emplace_back(command);
		}

		[[nodiscard]] CommandGraphDescriptor Build() const {
			ValidateCommandGraphDescriptor(m_descriptor);
			CommandGraphTopologicalOrder(m_descriptor);
			return m_descriptor;
		}
	};

}
