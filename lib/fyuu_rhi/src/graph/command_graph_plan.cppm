module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:command_graph_plan;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :command_graph_types;
import :command_graph_validation;

namespace {

	using namespace fyuu_rhi::execution;

}

namespace fyuu_rhi::execution {
	struct SubmissionBatchID {
		std::uint32_t value;

		std::strong_ordering operator<=>(SubmissionBatchID const&) const noexcept = default;
	};

	struct GraphBarrierPlan {
		GraphResourceID resource;
		GraphNodeID source;
		GraphNodeID destination;
		SubmissionBatchID source_batch;
		SubmissionBatchID destination_batch;
		GraphNodeFlagBits source_queue;
		GraphNodeFlagBits destination_queue;
		GraphAccessFlagBits source_access;
		GraphAccessFlagBits destination_access;
		GraphSubresourceRange source_range;
		GraphSubresourceRange destination_range;

		[[nodiscard]] bool CrossCapability() const noexcept {
			return source_queue != destination_queue;
		}
	};

	struct SubmissionBatchPlan {
		SubmissionBatchID id;
		GraphNodeFlagBits queue_flags;
		std::vector<GraphNodeID> nodes;
		std::vector<SubmissionBatchID> dependencies;
		std::vector<GraphBarrierPlan> release_barriers;
		std::vector<GraphBarrierPlan> barriers;
	};

	struct CommandGraphPlan {
		std::vector<GraphNodeID> topological_order;
		std::vector<GraphNodeID> last_resource_users;
		std::vector<SubmissionBatchPlan> batches;
	};

	void ValidateRenderingScopes(
		CommandGraphDescriptor const& descriptor,
		CommandGraphPlan const& plan
	) {
		for (auto const& batch : plan.batches) {
			bool rendering = false;
			for (auto node_id : batch.nodes) {
				for (auto const& command : descriptor.nodes[node_id.value].commands) {
					if (std::holds_alternative<BeginRenderingCommand>(command)) {
						if (rendering) {
							throw std::invalid_argument("Command graph contains nested rendering scopes");
						}
						rendering = true;
					}
					else if (std::holds_alternative<EndRenderingCommand>(command)) {
						if (!rendering) {
							throw std::invalid_argument("EndRendering has no matching BeginRendering");
						}
						rendering = false;
					}
					else if ((std::holds_alternative<DrawCommand>(command) ||
						std::holds_alternative<DrawIndexedCommand>(command)) && !rendering) {
						throw std::invalid_argument("Draw commands require an active rendering scope");
					}
					else if ((std::holds_alternative<DispatchCommand>(command) ||
						std::holds_alternative<CopyBufferCommand>(command) ||
						std::holds_alternative<PresentCommand>(command)) && rendering) {
						throw std::invalid_argument(
							"Dispatch, copy, and presentation commands cannot execute in a rendering scope"
						);
					}
				}
			}
			if (rendering) {
				throw std::invalid_argument("Rendering scope must end in the batch where it begins");
			}
		}
	}

	CommandGraphPlan CompileCommandGraphPlan(CommandGraphDescriptor const& descriptor) {
		ValidateCommandGraphDescriptor(descriptor);
		CommandGraphPlan plan;
		plan.topological_order = CommandGraphTopologicalOrder(descriptor);
		plan.last_resource_users.assign(
			descriptor.resource_count,
			GraphNodeID{ (std::numeric_limits<std::uint32_t>::max)() }
		);
		for (auto node_id : plan.topological_order) {
			for (auto const& access : descriptor.nodes[node_id.value].accesses) {
				plan.last_resource_users[access.resource.value] = node_id;
			}
		}

		std::vector<SubmissionBatchID> node_batches(descriptor.nodes.size());
		for (auto node_id : plan.topological_order) {
			auto const& node = descriptor.nodes[node_id.value];
			if (plan.batches.empty() || plan.batches.back().queue_flags != node.flags) {
				auto batch_id = SubmissionBatchID{ static_cast<std::uint32_t>(plan.batches.size()) };
				plan.batches.push_back({
					.id = batch_id,
					.queue_flags = node.flags
				});
			}
			plan.batches.back().nodes.emplace_back(node_id);
			node_batches[node_id.value] = plan.batches.back().id;
		}

		for (auto const& node : descriptor.nodes) {
			auto destination_batch = node_batches[node.id.value];
			auto& dependencies = plan.batches[destination_batch.value].dependencies;
			for (auto dependency : node.dependencies) {
				auto source_batch = node_batches[dependency.value];
				if (source_batch != destination_batch &&
					!std::ranges::contains(dependencies, source_batch)) {
					dependencies.emplace_back(source_batch);
				}
			}
		}

		struct PreviousAccess {
			GraphNodeID node;
			GraphResourceAccess access;
		};
		std::vector<std::vector<PreviousAccess>> previous_accesses(descriptor.resource_count);
		for (auto node_id : plan.topological_order) {
			auto const& node = descriptor.nodes[node_id.value];
			auto destination_batch = node_batches[node_id.value];
			for (auto const& access : node.accesses) {
				if (access.resource.value >= previous_accesses.size()) {
					throw std::invalid_argument("CompileCommandGraphPlan(): invalid resource ID");
				}
				for (auto const& previous : previous_accesses[access.resource.value]) {
					if (!GraphSubresourceRangesOverlap(previous.access.range, access.range)) {
						continue;
					}
					auto source_batch = node_batches[previous.node.value];
					GraphBarrierPlan barrier{
						.resource = access.resource,
						.source = previous.node,
						.destination = node_id,
						.source_batch = source_batch,
						.destination_batch = destination_batch,
						.source_queue = plan.batches[source_batch.value].queue_flags,
						.destination_queue = plan.batches[destination_batch.value].queue_flags,
						.source_access = previous.access.flags,
						.destination_access = access.flags,
						.source_range = previous.access.range,
						.destination_range = access.range
					};
					plan.batches[destination_batch.value].barriers.emplace_back(barrier);
					if (barrier.CrossCapability()) {
						plan.batches[source_batch.value].release_barriers.emplace_back(barrier);
					}
				}
				auto& resource_accesses = previous_accesses[access.resource.value];
				for (auto previous = resource_accesses.begin(); previous != resource_accesses.end();) {
					if (GraphSubresourceRangesOverlap(previous->access.range, access.range)) {
						previous = resource_accesses.erase(previous);
					}
					else {
						++previous;
					}
				}
				resource_accesses.push_back({ node_id, access });
			}
		}

		ValidateRenderingScopes(descriptor, plan);
		return plan;
	}

}
