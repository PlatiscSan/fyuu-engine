module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <type_traits>

#include <optional>
#include <variant>

#include <compare>
#include <concepts>
#include <stop_token>
#endif // !defined(__cpp_lib_modules)
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
#include <execution>
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

export module fyuu_rhi:execution;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

import :core_types;
import :resource_types;
import :resource;
import :view;
import :sampler;
import :pipeline;
export import :execution_types;

namespace {
	using namespace fyuu_rhi;
	using namespace fyuu_rhi::execution;

	bool CanRead(AccessMode mode) noexcept {
		return mode == AccessMode::Read || mode == AccessMode::ReadWrite;
	}

	bool CanWrite(AccessMode mode) noexcept {
		return mode == AccessMode::Write || mode == AccessMode::ReadWrite;
	}

	void ValidateIndex(std::size_t index, std::size_t count, char const* message) {
		if (index >= count) {
			throw std::out_of_range(message);
		}
	}

	void ValidateTextureRegion(TextureRegion const& region, char const* message) {
		if (region.width == 0u || region.height == 0u || region.depth == 0u ||
			region.array_layer_count == 0u) {
			throw std::invalid_argument(message);
		}
	}

	void ValidateTextureLayout(TextureDataLayout const& layout, char const* message) {
		if (layout.bytes_per_row == 0u || layout.rows_per_image == 0u) {
			throw std::invalid_argument(message);
		}
	}

	void ValidateRange(ResourceRange const& range) {
		if (auto buffer = std::get_if<BufferRange>(&range)) {
			if (buffer->size == 0u ||
				buffer->offset > (std::numeric_limits<std::size_t>::max)() - buffer->size) {
				throw std::invalid_argument("Command graph contains an invalid buffer range");
			}
		}
		else if (auto texture = std::get_if<TextureRange>(&range)) {
			if (texture->mip_level_count == 0u || texture->array_layer_count == 0u ||
				texture->base_mip_level >
					(std::numeric_limits<std::uint32_t>::max)() - texture->mip_level_count ||
				texture->base_array_layer >
					(std::numeric_limits<std::uint32_t>::max)() - texture->array_layer_count) {
				throw std::invalid_argument("Command graph contains an invalid texture range");
			}
		}
	}

	[[nodiscard]] bool RangesOverlap(ResourceRange const& lhs, ResourceRange const& rhs) noexcept {
		if (std::holds_alternative<std::monostate>(lhs) ||
			std::holds_alternative<std::monostate>(rhs)) {
			return true;
		}
		if (auto lhs_buffer = std::get_if<BufferRange>(&lhs)) {
			auto rhs_buffer = std::get_if<BufferRange>(&rhs);
			if (!rhs_buffer) {
				return true;
			}
			return lhs_buffer->offset < rhs_buffer->offset + rhs_buffer->size &&
				rhs_buffer->offset < lhs_buffer->offset + lhs_buffer->size;
		}
		auto const& lhs_texture = std::get<TextureRange>(lhs);
		auto rhs_texture = std::get_if<TextureRange>(&rhs);
		if (!rhs_texture) {
			return true;
		}
		bool mip_overlap =
			lhs_texture.base_mip_level < rhs_texture->base_mip_level + rhs_texture->mip_level_count &&
			rhs_texture->base_mip_level < lhs_texture.base_mip_level + lhs_texture.mip_level_count;
		bool layer_overlap =
			lhs_texture.base_array_layer <
				rhs_texture->base_array_layer + rhs_texture->array_layer_count &&
			rhs_texture->base_array_layer <
				lhs_texture.base_array_layer + lhs_texture.array_layer_count;
		return mip_overlap && layer_overlap;
	}

	[[nodiscard]] bool RangeContains(ResourceRange const& outer, ResourceRange const& inner) noexcept {
		if (std::holds_alternative<std::monostate>(outer)) {
			return true;
		}
		if (std::holds_alternative<std::monostate>(inner)) {
			return false;
		}
		if (auto outer_buffer = std::get_if<BufferRange>(&outer)) {
			auto inner_buffer = std::get_if<BufferRange>(&inner);
			return inner_buffer && outer_buffer->offset <= inner_buffer->offset &&
				outer_buffer->offset + outer_buffer->size >=
					inner_buffer->offset + inner_buffer->size;
		}
		auto const& outer_texture = std::get<TextureRange>(outer);
		auto inner_texture = std::get_if<TextureRange>(&inner);
		return inner_texture &&
			outer_texture.base_mip_level <= inner_texture->base_mip_level &&
			outer_texture.base_mip_level + outer_texture.mip_level_count >=
				inner_texture->base_mip_level + inner_texture->mip_level_count &&
			outer_texture.base_array_layer <= inner_texture->base_array_layer &&
			outer_texture.base_array_layer + outer_texture.array_layer_count >=
				inner_texture->base_array_layer + inner_texture->array_layer_count;
	}

	[[nodiscard]] ResourceRange IntersectRanges(ResourceRange const& lhs, ResourceRange const& rhs) noexcept {
		if (std::holds_alternative<std::monostate>(lhs)) {
			return rhs;
		}
		if (std::holds_alternative<std::monostate>(rhs)) {
			return lhs;
		}
		if (auto lhs_buffer = std::get_if<BufferRange>(&lhs)) {
			auto rhs_buffer = std::get_if<BufferRange>(&rhs);
			if (!rhs_buffer) {
				return std::monostate{};
			}
			auto begin = (std::max)(lhs_buffer->offset, rhs_buffer->offset);
			auto end = (std::min)(
				lhs_buffer->offset + lhs_buffer->size,
				rhs_buffer->offset + rhs_buffer->size
			);
			return BufferRange{ begin, end - begin };
		}
		auto const& lhs_texture = std::get<TextureRange>(lhs);
		auto rhs_texture = std::get_if<TextureRange>(&rhs);
		if (!rhs_texture) {
			return std::monostate{};
		}
		auto mip = (std::max)(lhs_texture.base_mip_level, rhs_texture->base_mip_level);
		auto mip_end = (std::min)(
			lhs_texture.base_mip_level + lhs_texture.mip_level_count,
			rhs_texture->base_mip_level + rhs_texture->mip_level_count
		);
		auto layer = (std::max)(
			lhs_texture.base_array_layer,
			rhs_texture->base_array_layer
		);
		auto layer_end = (std::min)(
			lhs_texture.base_array_layer + lhs_texture.array_layer_count,
			rhs_texture->base_array_layer + rhs_texture->array_layer_count
		);
		return TextureRange{
			.base_mip_level = mip,
			.mip_level_count = mip_end - mip,
			.base_array_layer = layer,
			.array_layer_count = layer_end - layer
		};
	}

	[[nodiscard]] bool MergeRanges(ResourceRange& destination, ResourceRange const& source) noexcept {
		if (std::holds_alternative<std::monostate>(destination)) {
			return true;
		}
		if (std::holds_alternative<std::monostate>(source)) {
			destination = std::monostate{};
			return true;
		}
		if (auto destination_buffer = std::get_if<BufferRange>(&destination)) {
			auto source_buffer = std::get_if<BufferRange>(&source);
			if (!source_buffer) {
				return false;
			}
			auto destination_end = destination_buffer->offset + destination_buffer->size;
			auto source_end = source_buffer->offset + source_buffer->size;
			if (destination_end < source_buffer->offset || source_end < destination_buffer->offset) {
				return false;
			}
			auto begin = (std::min)(destination_buffer->offset, source_buffer->offset);
			auto end = (std::max)(destination_end, source_end);
			*destination_buffer = BufferRange{ begin, end - begin };
			return true;
		}
		auto destination_texture = std::get_if<TextureRange>(&destination);
		auto source_texture = std::get_if<TextureRange>(&source);
		if (!source_texture) {
			return false;
		}
		bool same_layers =
			destination_texture->base_array_layer == source_texture->base_array_layer &&
			destination_texture->array_layer_count == source_texture->array_layer_count;
		bool same_mips =
			destination_texture->base_mip_level == source_texture->base_mip_level &&
			destination_texture->mip_level_count == source_texture->mip_level_count;
		if (same_layers) {
			auto destination_end =
				destination_texture->base_mip_level + destination_texture->mip_level_count;
			auto source_end = source_texture->base_mip_level + source_texture->mip_level_count;
			if (destination_end < source_texture->base_mip_level ||
				source_end < destination_texture->base_mip_level) {
				return false;
			}
			auto begin = (std::min)(
				destination_texture->base_mip_level,
				source_texture->base_mip_level
			);
			auto end = (std::max)(destination_end, source_end);
			destination_texture->base_mip_level = begin;
			destination_texture->mip_level_count = end - begin;
			return true;
		}
		if (same_mips) {
			auto destination_end =
				destination_texture->base_array_layer + destination_texture->array_layer_count;
			auto source_end = source_texture->base_array_layer + source_texture->array_layer_count;
			if (destination_end < source_texture->base_array_layer ||
				source_end < destination_texture->base_array_layer) {
				return false;
			}
			auto begin = (std::min)(
				destination_texture->base_array_layer,
				source_texture->base_array_layer
			);
			auto end = (std::max)(destination_end, source_end);
			destination_texture->base_array_layer = begin;
			destination_texture->array_layer_count = end - begin;
			return true;
		}
		return false;
	}

	void AddDependency(std::vector<std::size_t>& dependencies, std::size_t dependency) {
		if (std::ranges::find(dependencies, dependency) == dependencies.end()) {
			dependencies.emplace_back(dependency);
		}
	}

	void AddBoundaryAccess(
		std::vector<ExecutionBoundaryAccess>& accesses,
		ExecutionBoundaryAccess const& access
	) {
		for (auto const& existing : accesses) {
			if (RangeContains(existing.range, access.range)) {
				return;
			}
		}
		accesses.emplace_back(access);
	}

	[[nodiscard]] bool SameBarrierTransition(
		ExecutionBarrier const& lhs,
		ExecutionBarrier const& rhs
	) noexcept {
		return lhs.resource == rhs.resource &&
			lhs.source_node == rhs.source_node &&
			lhs.destination_node == rhs.destination_node &&
			lhs.source_batch == rhs.source_batch &&
			lhs.destination_batch == rhs.destination_batch &&
			lhs.source_queue == rhs.source_queue &&
			lhs.destination_queue == rhs.destination_queue &&
			lhs.source_mode == rhs.source_mode &&
			lhs.destination_mode == rhs.destination_mode &&
			lhs.source_usage == rhs.source_usage &&
			lhs.destination_usage == rhs.destination_usage;
	}

	void AddBarrier(std::vector<ExecutionBarrier>& barriers, ExecutionBarrier const& barrier) {
		for (auto& existing : barriers) {
			if (SameBarrierTransition(existing, barrier) &&
				MergeRanges(existing.source_range, barrier.source_range)) {
				existing.destination_range = existing.source_range;
				return;
			}
		}
		barriers.emplace_back(barrier);
	}

	[[nodiscard]] bool RequiresBarrier(
		ResourceAccess const& source,
		QueueType source_queue,
		ResourceAccess const& destination,
		QueueType destination_queue
	) noexcept {
		return CanWrite(source.mode) || CanWrite(destination.mode) ||
			source.usage != destination.usage || source_queue != destination_queue;
	}

	void ValidateAccess(
		ExecutionNode const& node,
		std::size_t resource,
		bool read,
		bool write,
		ResourceUsage usage,
		char const* message
	) {
		for (auto const& access : node.accesses) {
			if (access.resource == resource &&
				(!read || CanRead(access.mode)) &&
				(!write || CanWrite(access.mode)) &&
				HasUsage(access.usage, usage)) {
				return;
			}
		}
		throw std::invalid_argument(message);
	}

	struct CommandValidator {
		ExecutionGraph const& graph;
		ExecutionNode const& node;

		void RequireQueue(QueueType queue, char const* message) const {
			if (node.queue != queue) {
				throw std::invalid_argument(message);
			}
		}

		void ValidateCopy(std::size_t source, std::size_t destination) const {
			RequireQueue(QueueType::Transfer, "Copy command requires a transfer node");
			ValidateIndex(source, graph.bindings.resource_count,
				"Copy command contains an invalid source resource");
			ValidateIndex(destination, graph.bindings.resource_count,
				"Copy command contains an invalid destination resource");
			ValidateAccess(node, source, true, false, ResourceUsage::CopySource,
				"Copy source is missing its read copy-source access");
			ValidateAccess(node, destination, false, true, ResourceUsage::CopyDestination,
				"Copy destination is missing its write copy-destination access");
		}

		void operator()(BeginRendering const& command) const {
			RequireQueue(QueueType::Graphics, "BeginRendering requires a graphics node");
			if (command.area.x < 0 || command.area.y < 0 ||
				command.area.width == 0u || command.area.height == 0u) {
				throw std::invalid_argument("BeginRendering contains an invalid rendering area");
			}
			for (auto const& color : command.colors) {
				ValidateIndex(color.resource, graph.bindings.resource_count,
					"Color attachment contains an invalid resource");
				ValidateIndex(color.view, graph.bindings.view_count,
					"Color attachment contains an invalid view");
				ValidateAccess(
					node,
					color.resource,
					color.load == LoadOperation::Load,
					true,
					ResourceUsage::ColorAttachment,
					"Color attachment is missing its declared attachment access"
				);
				if (color.resolve_resource.has_value() != color.resolve_view.has_value()) {
					throw std::invalid_argument(
						"Resolve attachment requires both a resource and a view"
					);
				}
				if (color.resolve_resource) {
					ValidateIndex(*color.resolve_resource, graph.bindings.resource_count,
						"Resolve attachment contains an invalid resource");
					ValidateIndex(*color.resolve_view, graph.bindings.view_count,
						"Resolve attachment contains an invalid view");
					ValidateAccess(node, color.resource, true, false, ResourceUsage::ResolveSource,
						"Resolve source is missing its read resolve-source access");
					ValidateAccess(node, *color.resolve_resource, false, true,
						ResourceUsage::ResolveDestination,
						"Resolve destination is missing its write resolve-destination access");
				}
			}
			if (command.depth_stencil) {
				ValidateIndex(command.depth_stencil->resource, graph.bindings.resource_count,
					"Depth/stencil attachment contains an invalid resource");
				ValidateIndex(command.depth_stencil->view, graph.bindings.view_count,
					"Depth/stencil attachment contains an invalid view");
				bool read = command.depth_stencil->depth_load == LoadOperation::Load ||
					command.depth_stencil->stencil_load == LoadOperation::Load;
				bool write = command.depth_stencil->depth_load == LoadOperation::Clear ||
					command.depth_stencil->stencil_load == LoadOperation::Clear ||
					command.depth_stencil->depth_store == StoreOperation::Store ||
					command.depth_stencil->stencil_store == StoreOperation::Store;
				ValidateAccess(node, command.depth_stencil->resource, read, write,
					ResourceUsage::DepthStencilAttachment,
					"Depth/stencil attachment is missing its declared attachment access");
			}
		}

		void operator()(EndRendering const&) const {
			RequireQueue(QueueType::Graphics, "EndRendering requires a graphics node");
		}

		void operator()(BindPipeline const& command) const {
			if (node.queue != QueueType::Graphics && node.queue != QueueType::Compute) {
				throw std::invalid_argument("BindPipeline requires a graphics or compute node");
			}
			ValidateIndex(command.pipeline, graph.bindings.pipeline_count,
				"BindPipeline contains an invalid pipeline");
		}

		void operator()(BindResourceGroup const& command) const {
			if (node.queue != QueueType::Graphics && node.queue != QueueType::Compute) {
				throw std::invalid_argument(
					"BindResourceGroup requires a graphics or compute node"
				);
			}
			ValidateIndex(command.group, graph.bindings.resource_group_count,
				"BindResourceGroup contains an invalid resource group");
		}

		void operator()(BindVertexBuffer const& command) const {
			RequireQueue(QueueType::Graphics, "BindVertexBuffer requires a graphics node");
			ValidateIndex(command.resource, graph.bindings.resource_count,
				"BindVertexBuffer contains an invalid resource");
			if (command.stride == 0u) {
				throw std::invalid_argument("BindVertexBuffer stride must be non-zero");
			}
			ValidateAccess(node, command.resource, true, false, ResourceUsage::VertexBuffer,
				"Vertex buffer is missing its read vertex-buffer access");
		}

		void operator()(BindIndexBuffer const& command) const {
			RequireQueue(QueueType::Graphics, "BindIndexBuffer requires a graphics node");
			ValidateIndex(command.resource, graph.bindings.resource_count,
				"BindIndexBuffer contains an invalid resource");
			ValidateAccess(node, command.resource, true, false, ResourceUsage::IndexBuffer,
				"Index buffer is missing its read index-buffer access");
		}

		void operator()(Viewport const& command) const {
			RequireQueue(QueueType::Graphics, "Viewport requires a graphics node");
			if (command.width < 0.0f || command.height < 0.0f ||
				command.minimum_depth < 0.0f || command.maximum_depth > 1.0f ||
				command.minimum_depth > command.maximum_depth) {
				throw std::invalid_argument("Viewport contains invalid bounds");
			}
		}

		void operator()(Scissor const& command) const {
			RequireQueue(QueueType::Graphics, "Scissor requires a graphics node");
			if (command.x < 0 || command.y < 0) {
				throw std::invalid_argument("Scissor contains a negative offset");
			}
		}

		void operator()(Draw const&) const {
			RequireQueue(QueueType::Graphics, "Draw requires a graphics node");
		}

		void operator()(DrawIndexed const&) const {
			RequireQueue(QueueType::Graphics, "DrawIndexed requires a graphics node");
		}

		void operator()(Dispatch const& command) const {
			RequireQueue(QueueType::Compute, "Dispatch requires a compute node");
			if (command.group_count_x == 0u || command.group_count_y == 0u ||
				command.group_count_z == 0u) {
				throw std::invalid_argument("Dispatch group counts must be non-zero");
			}
		}

		void operator()(CopyBuffer const& command) const {
			ValidateCopy(command.source, command.destination);
			if (command.size == 0u) {
				throw std::invalid_argument("CopyBuffer size must be non-zero");
			}
		}

		void operator()(CopyBufferToTexture const& command) const {
			ValidateCopy(command.source, command.destination);
			ValidateTextureLayout(command.source_layout,
				"CopyBufferToTexture contains an invalid source layout");
			ValidateTextureRegion(command.destination_region,
				"CopyBufferToTexture contains an invalid destination region");
		}

		void operator()(CopyTextureToBuffer const& command) const {
			ValidateCopy(command.source, command.destination);
			ValidateTextureRegion(command.source_region,
				"CopyTextureToBuffer contains an invalid source region");
			ValidateTextureLayout(command.destination_layout,
				"CopyTextureToBuffer contains an invalid destination layout");
		}

		void operator()(CopyTexture const& command) const {
			ValidateCopy(command.source, command.destination);
			ValidateTextureRegion(command.source_region,
				"CopyTexture contains an invalid source region");
			ValidateTextureRegion(command.destination_region,
				"CopyTexture contains an invalid destination region");
			if (command.source_region.width != command.destination_region.width ||
				command.source_region.height != command.destination_region.height ||
				command.source_region.depth != command.destination_region.depth ||
				command.source_region.array_layer_count !=
					command.destination_region.array_layer_count) {
				throw std::invalid_argument("CopyTexture source and destination extents differ");
			}
		}

		void operator()(WriteBuffer const& command) const {
			RequireQueue(QueueType::Transfer, "WriteBuffer requires a transfer node");
			ValidateIndex(command.resource, graph.bindings.resource_count,
				"WriteBuffer contains an invalid resource");
			if (command.data.empty()) {
				throw std::invalid_argument("WriteBuffer data must not be empty");
			}
			if (command.offset > (std::numeric_limits<std::size_t>::max)() - command.data.size()) {
				throw std::invalid_argument("WriteBuffer range overflows");
			}
			// WriteBuffer is a host-side write (D3D12 Map / vmaMapMemory / MapAsync):
			// it does not change the GPU-side resource state, so it declares no
			// access of its own. The target only needs a read/copy-source access if
			// it is subsequently consumed by a CopyBuffer/CopyBufferToTexture.
		}

		void operator()(Present const& command) const {
			RequireQueue(QueueType::Present, "Present requires a presentation node");
			ValidateIndex(command.source, graph.bindings.resource_count,
				"Present contains an invalid source resource");
			if (command.buffer_count == 0u) {
				throw std::invalid_argument("Present buffer count must not be zero");
			}
			ValidateAccess(node, command.source, true, false,
				ResourceUsage::PresentationSource,
				"Presentation source is missing its read presentation-source access");
		}
	};

	[[nodiscard]] std::size_t TakeReadyNode(
		std::vector<std::size_t>& ready,
		ExecutionGraph const& graph,
		std::optional<QueueType> preferred_queue
	) {
		std::size_t selected = 0u;
		for (std::size_t index = 1u; index < ready.size(); ++index) {
			bool selected_preferred = preferred_queue &&
				graph.nodes[ready[selected]].queue == *preferred_queue;
			bool candidate_preferred = preferred_queue &&
				graph.nodes[ready[index]].queue == *preferred_queue;
			if ((candidate_preferred && !selected_preferred) ||
				(candidate_preferred == selected_preferred && ready[index] < ready[selected])) {
				selected = index;
			}
		}
		auto node = ready[selected];
		ready.erase(ready.begin() + static_cast<std::ptrdiff_t>(selected));
		return node;
	}

	[[nodiscard]] ExecutionPlan CompileExecutionPlanImpl(ExecutionGraph const& graph) {
		std::vector<std::size_t> indegrees(graph.nodes.size(), 0u);
		std::vector<std::vector<std::size_t>> dependents(graph.nodes.size());
		for (std::size_t index = 0u; index < graph.nodes.size(); ++index) {
			auto const& node = graph.nodes[index];
			if (node.id != index) {
				throw std::invalid_argument(
					"Command graph node IDs must match their storage indices"
				);
			}
			for (std::size_t dependency_index = 0u; dependency_index < node.dependencies.size(); ++dependency_index) {
				auto dependency = node.dependencies[dependency_index];
				if (dependency >= graph.nodes.size() || dependency == node.id) {
					throw std::invalid_argument("Command graph contains an invalid dependency");
				}
				for (std::size_t previous = 0u; previous < dependency_index; ++previous) {
					if (node.dependencies[previous] == dependency) {
						throw std::invalid_argument(
							"Command graph contains a duplicate dependency"
						);
					}
				}
				dependents[dependency].emplace_back(node.id);
			}
			indegrees[node.id] = node.dependencies.size();
			for (auto const& access : node.accesses) {
				ValidateIndex(access.resource, graph.bindings.resource_count,
					"Command graph access contains an invalid resource");
				if (access.usage == ResourceUsage::None) {
					throw std::invalid_argument("Command graph access usage must not be empty");
				}
				ValidateRange(access.range);
			}

			bool rendering = false;
			for (auto const& command : node.commands) {
				if (std::holds_alternative<BeginRendering>(command)) {
					if (rendering) {
						throw std::invalid_argument("Command graph contains nested rendering scopes");
					}
					rendering = true;
				}
				else if (std::holds_alternative<EndRendering>(command)) {
					if (!rendering) {
						throw std::invalid_argument(
							"EndRendering has no matching BeginRendering"
						);
					}
					rendering = false;
				}
				else if ((std::holds_alternative<Draw>(command) ||
					std::holds_alternative<DrawIndexed>(command)) && !rendering) {
					throw std::invalid_argument("Draw commands require an active rendering scope");
				}
				else if ((std::holds_alternative<Dispatch>(command) ||
					std::holds_alternative<CopyBuffer>(command) ||
					std::holds_alternative<CopyBufferToTexture>(command) ||
					std::holds_alternative<CopyTextureToBuffer>(command) ||
					std::holds_alternative<CopyTexture>(command) ||
					std::holds_alternative<WriteBuffer>(command) ||
					std::holds_alternative<Present>(command)) && rendering) {
					throw std::invalid_argument(
						"Dispatch, copy, and presentation commands cannot execute in a rendering scope"
					);
				}
				std::visit(CommandValidator{ graph, node }, command);
			}
			if (rendering) {
				throw std::invalid_argument(
					"Rendering scope must end in the node where it begins"
				);
			}
		}

		std::vector<std::size_t> ready;
		for (std::size_t index = 0u; index < indegrees.size(); ++index) {
			if (indegrees[index] == 0u) {
				ready.emplace_back(index);
			}
		}
		ExecutionPlan plan;
		plan.bindings = graph.bindings;
		plan.topological_order.reserve(graph.nodes.size());
		std::optional<QueueType> preferred_queue;
		while (!ready.empty()) {
			auto node = TakeReadyNode(ready, graph, preferred_queue);
			plan.topological_order.emplace_back(node);
			preferred_queue = graph.nodes[node].queue;
			for (auto dependent : dependents[node]) {
				if (--indegrees[dependent] == 0u) {
					ready.emplace_back(dependent);
				}
			}
		}
		if (plan.topological_order.size() != graph.nodes.size()) {
			throw std::invalid_argument("Command graph contains a dependency cycle");
		}

		auto nodes = graph.nodes;
		struct PreviousAccess {
			std::size_t node;
			ResourceAccess access;
		};
		std::vector<std::vector<PreviousAccess>> previous_accesses(graph.bindings.resource_count);
		std::vector<ExecutionBarrier> barriers;
		for (auto node_id : plan.topological_order) {
			for (auto const& access : nodes[node_id].accesses) {
				auto& previous = previous_accesses[access.resource];
				for (auto const& source : previous) {
					if (!RangesOverlap(source.access.range, access.range) ||
						!RequiresBarrier(
							source.access,
							nodes[source.node].queue,
							access,
							nodes[node_id].queue
						)) {
						continue;
					}
					AddDependency(nodes[node_id].dependencies, source.node);
					auto range = IntersectRanges(source.access.range, access.range);
					AddBarrier(barriers, ExecutionBarrier{
						.resource = access.resource,
						.source_node = source.node,
						.destination_node = node_id,
						.source_queue = nodes[source.node].queue,
						.destination_queue = nodes[node_id].queue,
						.source_mode = source.access.mode,
						.destination_mode = access.mode,
						.source_usage = source.access.usage,
						.destination_usage = access.usage,
						.source_range = range,
						.destination_range = range
					});
				}
				for (auto previous_access = previous.begin(); previous_access != previous.end();) {
					bool replace = RangesOverlap(previous_access->access.range, access.range) &&
						RequiresBarrier(
							previous_access->access,
							nodes[previous_access->node].queue,
							access,
							nodes[node_id].queue
						);
					if (replace) {
						previous_access = previous.erase(previous_access);
					}
					else {
						++previous_access;
					}
				}
				previous.emplace_back(PreviousAccess{ node_id, access });
			}
		}

		plan.node_batches.resize(graph.nodes.size());
		for (auto node_id : plan.topological_order) {
			auto const& node = nodes[node_id];
			if (plan.batches.empty() || plan.batches.back().queue != node.queue) {
				auto batch_id = plan.batches.size();
				plan.batches.push_back(
					{
						.id = batch_id,
						.queue = node.queue
					}
				);
			}
			plan.node_batches[node_id] = plan.batches.back().id;
		}

		for (auto const& node : nodes) {
			auto destination_batch = plan.node_batches[node.id];
			auto& dependencies = plan.batches[destination_batch].dependencies;
			for (auto dependency : node.dependencies) {
				auto source_batch = plan.node_batches[dependency];
				if (source_batch != destination_batch) {
					AddDependency(dependencies, source_batch);
				}
			}
		}
		for (auto& barrier : barriers) {
			barrier.source_batch = plan.node_batches[barrier.source_node];
			barrier.destination_batch = plan.node_batches[barrier.destination_node];
			AddBarrier(plan.batches[barrier.destination_batch].barriers, barrier);
			if (barrier.CrossQueue()) {
				AddBarrier(plan.batches[barrier.source_batch].release_barriers, barrier);
			}
		}
		plan.first_accesses.resize(graph.bindings.resource_count);
		plan.last_accesses.resize(graph.bindings.resource_count);
		for (auto node_id : plan.topological_order) {
			for (auto const& access : nodes[node_id].accesses) {
				AddBoundaryAccess(
					plan.first_accesses[access.resource],
					ExecutionBoundaryAccess{
						.resource = access.resource,
						.node = node_id,
						.batch = plan.node_batches[node_id],
						.queue = nodes[node_id].queue,
						.mode = access.mode,
						.usage = access.usage,
						.range = access.range
					}
				);
			}
		}
		for (auto node = plan.topological_order.rbegin();
			node != plan.topological_order.rend();
			++node) {
			for (auto const& access : nodes[*node].accesses) {
				AddBoundaryAccess(
					plan.last_accesses[access.resource],
					ExecutionBoundaryAccess{
						.resource = access.resource,
						.node = *node,
						.batch = plan.node_batches[*node],
						.queue = nodes[*node].queue,
						.mode = access.mode,
						.usage = access.usage,
						.range = access.range
					}
				);
			}
		}
		for (auto node_id : plan.topological_order) {
			auto batch = plan.node_batches[node_id];
			plan.batches[batch].nodes.emplace_back(std::move(nodes[node_id]));
		}
		return plan;
	}


}


namespace fyuu_rhi::execution {
	export [[nodiscard]] ExecutionPlan CompileExecutionPlan(ExecutionGraph const& graph) {
		return CompileExecutionPlanImpl(graph);
	}

	export template <class Backend> class CommandScheduler;
	export template <class Backend> class CommandGraphBuilder;
	export template <class Backend, class Receiver> class CommandGraphBindings;

	export template <class Backend> class CommandGraphResources {
		std::vector<std::optional<Resource<Backend>>> m_resources;
		std::vector<std::optional<View<Backend>>> m_views;
		std::vector<std::optional<Sampler<Backend>>> m_samplers;
		std::vector<std::optional<pipeline::Pipeline<Backend>>> m_pipelines;
		std::vector<std::optional<pipeline::PipelineResourceGroup<Backend>>> m_resource_groups;

		template <class U, class Receiver>
		friend class CommandGraphBindings;

		template <class Value>
		[[nodiscard]] static Value Take(std::vector<std::optional<Value>>& values,std::size_t index) {
			if (index >= values.size()) {
				throw std::out_of_range("Command graph result index is out of range");
			}
			auto& value = values[index];
			if (!value) {
				throw std::logic_error("Command graph result object is unavailable");
			}
			Value result = std::move(*value);
			value.reset();
			return result;
		}

		CommandGraphResources(
			std::vector<std::optional<Resource<Backend>>>&& resources,
			std::vector<std::optional<View<Backend>>>&& views,
			std::vector<std::optional<Sampler<Backend>>>&& samplers,
			std::vector<std::optional<pipeline::Pipeline<Backend>>>&& pipelines,
			std::vector<std::optional<pipeline::PipelineResourceGroup<Backend>>>&& resource_groups
		) noexcept : m_resources(std::move(resources)),
			m_views(std::move(views)),
			m_samplers(std::move(samplers)),
			m_pipelines(std::move(pipelines)),
			m_resource_groups(std::move(resource_groups)) {

		}

	public:
		CommandGraphResources(CommandGraphResources const&) = delete;
		CommandGraphResources& operator=(CommandGraphResources const&) = delete;
		CommandGraphResources(CommandGraphResources&&) noexcept = default;
		CommandGraphResources& operator=(CommandGraphResources&&) noexcept = default;
		~CommandGraphResources() noexcept = default;

		[[nodiscard]] Resource<Backend> TakeResource(std::size_t index) {
			return Take(m_resources, index);
		}

		[[nodiscard]] View<Backend> TakeView(std::size_t index) {
			return Take(m_views, index);
		}

		[[nodiscard]] Sampler<Backend> TakeSampler(std::size_t index) {
			return Take(m_samplers, index);
		}

		[[nodiscard]] pipeline::Pipeline<Backend> TakePipeline(std::size_t index) {
			return Take(m_pipelines, index);
		}

		[[nodiscard]] pipeline::PipelineResourceGroup<Backend> TakeResourceGroup(
			std::size_t index
		) {
			return Take(m_resource_groups, index);
		}
	};

	export template <class Token> concept CommandCompletionToken =
		std::is_nothrow_move_constructible_v<Token> &&
		std::is_nothrow_move_assignable_v<Token> &&
		requires(Token& token, Token const& const_token) {
			{ token.Poll() } noexcept -> std::same_as<bool>;
			{ const_token.Error() } noexcept -> std::same_as<std::exception_ptr>;
			{ const_token.IsStopped() } noexcept -> std::same_as<bool>;
	};

	export template <class Backend> concept CommandExecutionBackend =
		CommandCompletionToken<typename Backend::CompletionToken> &&
		std::is_nothrow_copy_constructible_v<typename Backend::SchedulerContext> &&
		std::is_nothrow_destructible_v<typename Backend::SchedulerContext> &&
		std::equality_comparable<typename Backend::SchedulerContext> &&
		std::move_constructible<typename Backend::PlatformHandle> &&
		requires(
	typename Backend::SchedulerContext const& scheduler,
		ExecutionPlan const& plan,
		std::span<typename Backend::PlatformHandle const> presentation_targets,
		std::span<std::reference_wrapper<typename Backend::Resource> const> resources,
		std::span<std::reference_wrapper<typename Backend::View> const> views,
		std::span<std::reference_wrapper<typename Backend::Sampler> const> samplers,
		std::span<std::reference_wrapper<typename Backend::Pipeline> const> pipelines,
		std::span<std::reference_wrapper<typename Backend::PipelineResourceGroup> const> resource_groups,
		StopTokenView stop_token
		) {
			{
				Backend::ExecuteCommands(
					scheduler,
					plan,
					presentation_targets,
					resources,
					views,
					samplers,
					pipelines,
					resource_groups,
					stop_token
				)
			} -> std::same_as<typename Backend::CompletionToken>;
	};

#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
	export template <class Receiver, class Backend>
	concept CommandGraphReceiver =
		std::move_constructible<Receiver> &&
		requires(
			Receiver& receiver,
			Receiver&& completed_receiver,
			CommandGraphResources<Backend>&& resources,
			std::exception_ptr error
		) {
			{ receiver.RecoverBindings(std::move(resources)) } noexcept;
			std::execution::get_env(receiver);
			{ std::execution::set_value(
				std::move(completed_receiver),
				std::move(resources)
			) } noexcept;
			{ std::execution::set_error(std::move(completed_receiver), error) } noexcept;
			{ std::execution::set_stopped(std::move(completed_receiver)) } noexcept;
		};
#else
	export template <class Receiver, class Backend>
	concept CommandGraphReceiver =
		std::move_constructible<Receiver> &&
		requires(
			Receiver& receiver,
			Receiver&& completed_receiver,
			CommandGraphResources<Backend>&& resources,
			std::exception_ptr error
		) {
			{ receiver.RecoverBindings(std::move(resources)) } noexcept;
			receiver.get_env();
			{ std::move(completed_receiver).set_value(std::move(resources)) } noexcept;
			{ std::move(completed_receiver).set_error(error) } noexcept;
			{ std::move(completed_receiver).set_stopped() } noexcept;
		};
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

	export template <class Backend, class Receiver> class CommandGraphBindings {
	public:
		using SchedulerContext = typename Backend::SchedulerContext;
		using CompletionToken = typename Backend::CompletionToken;
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using operation_state_concept = std::execution::operation_state_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

	private:
		struct OperationStateContext {
			SchedulerContext scheduler;
			ExecutionPlan plan;
			std::vector<std::optional<Resource<Backend>>> resources;
			std::vector<std::optional<View<Backend>>> views;
			std::vector<std::optional<Sampler<Backend>>> samplers;
			std::vector<std::optional<pipeline::Pipeline<Backend>>> pipelines;
			std::vector<std::optional<pipeline::PipelineResourceGroup<Backend>>> resource_groups;
			std::vector<typename Backend::PlatformHandle> presentation_targets;
			Receiver receiver;
			std::once_flag start_flag;
			std::once_flag completed_flag;

			OperationStateContext(
				SchedulerContext const& scheduler_,
				ExecutionPlan&& plan_,
				Receiver receiver_
			) : scheduler(scheduler_),
				plan(std::move(plan_)),
				resources(plan.bindings.resource_count),
				views(plan.bindings.view_count),
				samplers(plan.bindings.sampler_count),
				pipelines(plan.bindings.pipeline_count),
				resource_groups(plan.bindings.resource_group_count),
				receiver(std::move(receiver_)) {

			}
		};

		struct CompletionContext {
			std::unique_ptr<OperationStateContext> operation;
			CompletionToken token;

			CompletionContext(
				std::unique_ptr<OperationStateContext>&& operation_,
				CompletionToken&& token_
			) noexcept : operation(std::move(operation_)),
				token(std::move(token_)) {

			}

			CompletionContext(CompletionContext const&) = delete;
			CompletionContext& operator=(CompletionContext const&) = delete;
			CompletionContext(CompletionContext&&) noexcept = default;
			CompletionContext& operator=(CompletionContext&&) noexcept = default;
			~CompletionContext() noexcept = default;
		};

		class CompletionService {
			std::atomic<std::deque<CompletionContext>*> m_tasks = nullptr;
			std::atomic<std::mutex*> m_mutex = nullptr;
			std::atomic<std::condition_variable*> m_condition = nullptr;
			std::jthread m_worker;

			static void RunWorker(
				std::stop_token stop_token,
				CompletionService* service
			) {
				service->Run(stop_token);
			}

			void Run(std::stop_token stop_token) {
				std::deque<CompletionContext> tasks;
				std::deque<CompletionContext> current;
				std::deque<CompletionContext> pending;
				std::mutex mutex;
				std::condition_variable condition;

				m_mutex.store(&mutex, std::memory_order::relaxed);
				m_condition.store(&condition, std::memory_order::relaxed);
				m_tasks.store(&tasks, std::memory_order::release);
				m_tasks.notify_one();

				while (!stop_token.stop_requested()) {
					{
						std::unique_lock<std::mutex> lock(mutex);
						condition.wait_for(lock, std::chrono::milliseconds(1u));
						current.swap(tasks);
					}
					if (stop_token.stop_requested()) {
						break;
					}
					while (!current.empty()) {
						auto task = std::move(current.front());
						current.pop_front();
						if (task.token.Poll()) {
							CommandGraphBindings::Complete(std::move(task));
						}
						else {
							pending.emplace_back(std::move(task));
						}
					}
					if (!pending.empty()) {
						std::unique_lock<std::mutex> lock(mutex);
						while (!pending.empty()) {
							tasks.emplace_back(std::move(pending.front()));
							pending.pop_front();
						}
					}
				}

				m_tasks.store(nullptr, std::memory_order::release);
				m_mutex.store(nullptr, std::memory_order::release);
				m_condition.store(nullptr, std::memory_order::release);
			}

			CompletionService()
				: m_worker(&CompletionService::RunWorker, this) {
				m_tasks.wait(nullptr, std::memory_order::acquire);

			}

		public:
			CompletionService(CompletionService const&) = delete;
			CompletionService& operator=(CompletionService const&) = delete;

			~CompletionService() noexcept {
				m_worker.request_stop();
				if (auto condition = m_condition.load(std::memory_order::acquire)) {
					condition->notify_one();
				}
			}

			[[nodiscard]] static CompletionService& Instance() {
				static CompletionService service;
				return service;
			}

			void Enqueue(std::unique_ptr<OperationStateContext>&& operation, CompletionToken&& token) {
				auto tasks = m_tasks.load(std::memory_order::acquire);
				auto mutex = m_mutex.load(std::memory_order::acquire);
				auto condition = m_condition.load(std::memory_order::acquire);
				{
					std::unique_lock<std::mutex> lock(*mutex);
					tasks->emplace_back(std::move(operation), std::move(token));
				}
				condition->notify_one();
			}
		};

		std::unique_ptr<OperationStateContext> m_context;

		template <class Value>
		static void BindAt(
			std::vector<std::optional<Value>>& bindings,
			std::size_t index,
			Value&& value
		) {
			if (index >= bindings.size()) {
				throw std::out_of_range("Command graph binding index is out of range");
			}
			if (bindings[index]) {
				throw std::logic_error("Command graph binding is already occupied");
			}
			bindings[index].emplace(std::move(value));
		}

		template <class Value>
		static void ValidateBindings(
			std::vector<std::optional<Value>> const& bindings,
			char const* message
		) {
			for (auto const& binding : bindings) {
				if (!binding) {
					throw std::invalid_argument(message);
				}
			}
		}

		void ValidateBindings() const {
			ValidateBindings(m_context->resources, "Command graph contains an unbound resource");
			ValidateBindings(m_context->views, "Command graph contains an unbound view");
			ValidateBindings(m_context->samplers, "Command graph contains an unbound sampler");
			ValidateBindings(m_context->pipelines, "Command graph contains an unbound pipeline");
			ValidateBindings(
				m_context->resource_groups,
				"Command graph contains an unbound pipeline resource group"
			);
		}

		void ValidatePresentationTargets() const {
			for (auto const& batch : m_context->plan.batches) {
				for (auto const& node : batch.nodes) {
					for (auto const& command : node.commands) {
						if (auto present = std::get_if<Present>(&command);
							present && present->target >= m_context->presentation_targets.size()) {
							throw std::invalid_argument(
								"Present contains an unbound presentation target"
							);
						}
					}
				}
			}
		}

		[[nodiscard]] static auto NativeResources(OperationStateContext& context) {
			std::vector<std::reference_wrapper<typename Backend::Resource>> result;
			result.reserve(context.resources.size());
			for (auto& resource : context.resources) {
				result.emplace_back(resource->GetLogicalDevicePassKey().GetImplementation());
			}
			return result;
		}

		[[nodiscard]] static auto NativeViews(OperationStateContext& context) {
			std::vector<std::reference_wrapper<typename Backend::View>> result;
			result.reserve(context.views.size());
			for (auto& view : context.views) {
				result.emplace_back(view->GetPassKey().GetImplementation());
			}
			return result;
		}

		[[nodiscard]] static auto NativeSamplers(OperationStateContext& context) {
			std::vector<std::reference_wrapper<typename Backend::Sampler>> result;
			result.reserve(context.samplers.size());
			for (auto& sampler : context.samplers) {
				result.emplace_back(sampler->GetPassKey().GetImplementation());
			}
			return result;
		}

		[[nodiscard]] static auto NativePipelines(OperationStateContext& context) {
			std::vector<std::reference_wrapper<typename Backend::Pipeline>> result;
			result.reserve(context.pipelines.size());
			for (auto& pipeline : context.pipelines) {
				result.emplace_back(pipeline->GetPassKey().GetImplementation());
			}
			return result;
		}

		[[nodiscard]] static auto NativeResourceGroups(OperationStateContext& context) {
			std::vector<std::reference_wrapper<typename Backend::PipelineResourceGroup>> result;
			result.reserve(context.resource_groups.size());
			for (auto& group : context.resource_groups) {
				result.emplace_back(group->GetPassKey().GetImplementation());
			}
			return result;
		}

		[[nodiscard]] static CommandGraphResources<Backend> ReleaseBindings(OperationStateContext& context) noexcept {
			return CommandGraphResources<Backend>(
				std::move(context.resources),
				std::move(context.views),
				std::move(context.samplers),
				std::move(context.pipelines),
				std::move(context.resource_groups)
			);
		}

		static void SetValue(std::unique_ptr<OperationStateContext>&& context) noexcept {
			std::call_once(
				context->completed_flag,
				[&context]() noexcept {
					auto resources = ReleaseBindings(*context);
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
					std::execution::set_value(std::move(context->receiver), std::move(resources));
#else
					std::move(context->receiver).set_value(std::move(resources));
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
				}
			);
		}

		static void SetError(std::unique_ptr<OperationStateContext>&& context, std::exception_ptr error) noexcept {
			std::call_once(
				context->completed_flag,
				[&context, error]() noexcept {
					context->receiver.RecoverBindings(ReleaseBindings(*context));
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
					std::execution::set_error(std::move(context->receiver), error);
#else
					std::move(context->receiver).set_error(error);
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
				}
			);
		}

		static void SetStopped(std::unique_ptr<OperationStateContext>&& context) noexcept {
			std::call_once(
				context->completed_flag,
				[&context]() noexcept {
					context->receiver.RecoverBindings(ReleaseBindings(*context));
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
					std::execution::set_stopped(std::move(context->receiver));
#else
					std::move(context->receiver).set_stopped();
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
				}
			);
		}

		static void Complete(CompletionContext&& context) noexcept {
			if (auto error = context.token.Error()) {
				SetError(std::move(context.operation), error);
			}
			else if (context.token.IsStopped()) {
				SetStopped(std::move(context.operation));
			}
			else {
				SetValue(std::move(context.operation));
			}
		}


	public:
		CommandGraphBindings(
			SchedulerContext const& ctx,
			ExecutionPlan&& plan,
			Receiver receiver
		) : m_context(std::make_unique<OperationStateContext>(
			ctx,
			std::move(plan),
			std::move(receiver)
		)) {
			static_assert(CommandCompletionToken<CompletionToken>);
			static_assert(CommandExecutionBackend<Backend>);

		}

		CommandGraphBindings(CommandGraphBindings const&) = delete;
		CommandGraphBindings& operator=(CommandGraphBindings const&) = delete;
		CommandGraphBindings(CommandGraphBindings&&) noexcept = default;
		CommandGraphBindings& operator=(CommandGraphBindings&&) noexcept = default;
		~CommandGraphBindings() noexcept = default;

		void BindResource(std::size_t index, Resource<Backend>&& resource) {
			BindAt(m_context->resources, index, std::move(resource));
		}

		void BindView(std::size_t index, View<Backend>&& view) {
			BindAt(m_context->views, index, std::move(view));
		}

		void BindSampler(std::size_t index, Sampler<Backend>&& sampler) {
			BindAt(m_context->samplers, index, std::move(sampler));
		}

		void BindPipeline(std::size_t index, pipeline::Pipeline<Backend>&& pipeline) {
			BindAt(m_context->pipelines, index, std::move(pipeline));
		}

		void BindResourceGroup(
			std::size_t index,
			pipeline::PipelineResourceGroup<Backend>&& resource_group
		) {
			BindAt(m_context->resource_groups, index, std::move(resource_group));
		}

		CommandGraphBindings& FromLastBinding(CommandGraphResources<Backend>&& bindings) {
			if (m_context->resources.size() != bindings.m_resources.size() ||
				m_context->views.size() != bindings.m_views.size() ||
				m_context->samplers.size() != bindings.m_samplers.size() ||
				m_context->pipelines.size() != bindings.m_pipelines.size() ||
				m_context->resource_groups.size() != bindings.m_resource_groups.size()) {
				throw std::invalid_argument("Previous command graph bindings do not match the current layout");
			}
			auto HasBinding = []<class Value>(std::vector<std::optional<Value>> const& values) {
				return std::ranges::any_of(values, [](auto const& value) { return value.has_value(); });
				};
			if (HasBinding(m_context->resources) ||
				HasBinding(m_context->views) ||
				HasBinding(m_context->samplers) ||
				HasBinding(m_context->pipelines) ||
				HasBinding(m_context->resource_groups)) {
				throw std::logic_error("Current command graph already contains bindings");
			}
			m_context->resources = std::move(bindings.m_resources);
			m_context->views = std::move(bindings.m_views);
			m_context->samplers = std::move(bindings.m_samplers);
			m_context->pipelines = std::move(bindings.m_pipelines);
			m_context->resource_groups = std::move(bindings.m_resource_groups);
			return *this;
		}

		template <class... PlatformHandleArgs>
		CommandGraphBindings& SetPresentationTarget(PlatformHandleArgs&&... args) {
			m_context->presentation_targets.emplace_back(std::forward<PlatformHandleArgs>(args)...);
			return *this;
		}

		void start() & noexcept {
			if (!m_context) {
				return;
			}
			std::call_once(
				m_context->start_flag,
				[this]() noexcept {
					try {
						auto Execute = [this](StopTokenView stop_token) {
							if (stop_token.stop_requested()) {
								SetStopped(std::move(m_context));
								return;
							}
							ValidateBindings();
							ValidatePresentationTargets();
							if (stop_token.stop_requested()) {
								SetStopped(std::move(m_context));
								return;
							}
							auto resources = NativeResources(*m_context);
							auto views = NativeViews(*m_context);
							auto samplers = NativeSamplers(*m_context);
							auto pipelines = NativePipelines(*m_context);
							auto resource_groups = NativeResourceGroups(*m_context);
							auto token = Backend::ExecuteCommands(
								m_context->scheduler,
								m_context->plan,
								m_context->presentation_targets,
								resources,
								views,
								samplers,
								pipelines,
								resource_groups,
								stop_token
							);
							if (token.IsStopped()) {
								SetStopped(std::move(m_context));
								return;
							}
							CompletionService::Instance().Enqueue(
								std::move(m_context),
								std::move(token)
							);
							};
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
						auto env = std::execution::get_env(m_context->receiver);
						if constexpr (requires { std::execution::get_stop_token(env); }) {
							auto stop_token = std::execution::get_stop_token(env);
							Execute(stop_token);
						}
						else {
							Execute({});
						}
#else
						auto env = m_context->receiver.get_env();
						if constexpr (requires { env.get_stop_token(); }) {
							auto stop_token = env.get_stop_token();
							Execute(stop_token);
						}
						else {
							Execute({});
						}
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
					}
					catch (...) {
						SetError(std::move(m_context), std::current_exception());
					}
				}
			);
		}
	};

	export template <class Backend> class CommandGraphBuilder {
	public:
		using Context = typename Backend::SchedulerContext;
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using sender_concept = std::execution::sender_t;
		using completion_signatures = std::execution::completion_signatures<
			std::execution::set_value_t(CommandGraphResources<Backend>),
			std::execution::set_error_t(std::exception_ptr),
			std::execution::set_stopped_t()
		>;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
	private:
		Context m_ctx;
		ExecutionGraph m_graph;

		[[nodiscard]] ExecutionNode& GetNode(std::size_t id) {
			if (id >= m_graph.nodes.size() || m_graph.nodes[id].id != id) {
				throw std::out_of_range("Command graph node does not exist");
			}
			return m_graph.nodes[id];
		}

	public:
		class Node {
			friend class CommandGraphBuilder;

			CommandGraphBuilder* m_builder;
			std::size_t m_index;

			Node(CommandGraphBuilder* builder, std::size_t index) noexcept
				: m_builder(builder),
				m_index(index) {

			}

		public:
			Node& DependsOn(Node const& dependency) {
				if (m_builder != dependency.m_builder) {
					throw std::invalid_argument("Command graph dependency belongs to another builder");
				}
				m_builder->GetNode(m_index).dependencies.emplace_back(dependency.m_index);
				return *this;
			}

			Node& Access(ResourceAccess const& access) {
				m_builder->GetNode(m_index).accesses.emplace_back(access);
				return *this;
			}

			template <class Command>
				requires std::constructible_from<CommandRecord, Command>
			Node& Record(Command&& command) {
				m_builder->GetNode(m_index).commands.emplace_back(std::forward<Command>(command));
				return *this;
			}
		};

		explicit CommandGraphBuilder(Context const& ctx) noexcept
			: m_ctx(ctx) {
			static_assert(
				std::is_nothrow_copy_constructible_v<Context>,
				"Scheduler context must be nothrow copy constructible"
			);
		}

		[[nodiscard]] std::size_t RegisterResource() noexcept {
			return m_graph.bindings.resource_count++;
		}

		[[nodiscard]] std::size_t RegisterView() noexcept {
			return m_graph.bindings.view_count++;
		}

		[[nodiscard]] std::size_t RegisterSampler() noexcept {
			return m_graph.bindings.sampler_count++;
		}

		[[nodiscard]] std::size_t RegisterPipeline() noexcept {
			return m_graph.bindings.pipeline_count++;
		}

		[[nodiscard]] std::size_t RegisterResourceGroup() noexcept {
			return m_graph.bindings.resource_group_count++;
		}

		[[nodiscard]] Node CreateNode(
			QueueType queue = QueueType::Graphics,
			std::optional<Node> const& dependency = std::nullopt
		) {
			auto id = m_graph.nodes.size();
			m_graph.nodes.push_back(
				{
					.id = id,
					.queue = queue
				}
			);
			Node node{ this, id };
			if (dependency) {
				node.DependsOn(*dependency);
			}
			return node;
		}

		template <class Receiver>
			requires CommandGraphReceiver<std::remove_cvref_t<Receiver>, Backend>
		[[nodiscard]] CommandGraphBindings<Backend, std::remove_cvref_t<Receiver>> connect(
			Receiver&& receiver
		) && {
			return {
				m_ctx,
				CompileExecutionPlan(m_graph),
				std::forward<Receiver>(receiver)
			};
		}
	};

	export template <class Backend> class CommandScheduler {
	public:
		using Context = typename Backend::SchedulerContext;
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using scheduler_concept = std::execution::scheduler_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

	private:
		Context m_ctx;

	public:
		explicit CommandScheduler(Context const& ctx) noexcept
			: m_ctx(ctx) {
			static_assert(
				std::is_nothrow_copy_constructible_v<Context>,
				"Scheduler context must be nothrow copy constructible"
			);
		}

		[[nodiscard]] CommandGraphBuilder<Backend> schedule() const noexcept {
			return CommandGraphBuilder<Backend>{ m_ctx };
		}

		std::strong_ordering operator<=>(CommandScheduler const&) const noexcept = default;
	};

} // namespace fyuu_rhi::execution
