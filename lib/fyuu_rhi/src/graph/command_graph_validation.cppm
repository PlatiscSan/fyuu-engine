module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <queue>
#include <stdexcept>
#include <variant>
#include <vector>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:command_graph_validation;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :command_graph_types;

namespace fyuu_rhi::execution {

	bool HasGraphFlags(GraphNodeFlagBits flags, GraphNodeFlagBits expected) noexcept {
		return (flags & expected) == expected;
	}

	bool HasGraphAccess(GraphAccessFlagBits flags, GraphAccessFlagBits expected) noexcept {
		return (flags & expected) == expected;
	}

	bool GraphSubresourceRangesOverlap(
		GraphSubresourceRange const& lhs,
		GraphSubresourceRange const& rhs
	) noexcept {
		if (lhs.size != 0u && rhs.size != 0u) {
			auto lhs_end = lhs.offset + lhs.size;
			auto rhs_end = rhs.offset + rhs.size;
			return lhs.offset < rhs_end && rhs.offset < lhs_end;
		}
		if (lhs.mip_level_count == 0u || rhs.mip_level_count == 0u ||
			lhs.array_layer_count == 0u || rhs.array_layer_count == 0u) {
			return true;
		}

		auto lhs_mip_end = lhs.base_mip_level + lhs.mip_level_count;
		auto rhs_mip_end = rhs.base_mip_level + rhs.mip_level_count;
		auto lhs_layer_end = lhs.base_array_layer + lhs.array_layer_count;
		auto rhs_layer_end = rhs.base_array_layer + rhs.array_layer_count;
		return lhs.base_mip_level < rhs_mip_end &&
			rhs.base_mip_level < lhs_mip_end &&
			lhs.base_array_layer < rhs_layer_end &&
			rhs.base_array_layer < lhs_layer_end;
	}

	std::vector<GraphNodeID> CommandGraphTopologicalOrder(
		CommandGraphDescriptor const& descriptor
	) {
		std::vector<std::uint32_t> indegrees(descriptor.nodes.size(), 0u);
		std::vector<std::vector<GraphNodeID>> dependents(descriptor.nodes.size());
		for (auto const& node : descriptor.nodes) {
			indegrees[node.id.value] = static_cast<std::uint32_t>(node.dependencies.size());
			for (auto dependency : node.dependencies) {
				dependents[dependency.value].emplace_back(node.id);
			}
		}

		std::queue<GraphNodeID> ready;
		for (std::uint32_t index = 0u; index < indegrees.size(); ++index) {
			if (indegrees[index] == 0u) {
				ready.push(GraphNodeID{ index });
			}
		}

		std::vector<GraphNodeID> result;
		result.reserve(descriptor.nodes.size());
		while (!ready.empty()) {
			auto node = ready.front();
			ready.pop();
			result.emplace_back(node);
			for (auto dependent : dependents[node.value]) {
				if (--indegrees[dependent.value] == 0u) {
					ready.push(dependent);
				}
			}
		}
		if (result.size() != descriptor.nodes.size()) {
			throw std::invalid_argument("Command graph contains a dependency cycle");
		}
		return result;
	}

	void ValidateResourceID(CommandGraphDescriptor const& descriptor, GraphResourceID id) {
		if (id.value >= descriptor.resource_count) {
			throw std::out_of_range("Command graph command contains an invalid resource ID");
		}
	}

	void ValidateViewID(CommandGraphDescriptor const& descriptor, GraphViewID id) {
		if (id.value >= descriptor.view_count) {
			throw std::out_of_range("Command graph command contains an invalid view ID");
		}
	}

	void ValidateNodeCapability(
		GraphNodeDescriptor const& node,
		GraphNodeFlagBits required,
		char const* message
	) {
		if (!HasGraphFlags(node.flags, required)) {
			throw std::invalid_argument(message);
		}
	}

	void ValidateResourceAccess(
		GraphNodeDescriptor const& node,
		GraphResourceID resource,
		GraphAccessFlagBits required,
		char const* message
	) {
		for (auto const& access : node.accesses) {
			if (access.resource == resource && HasGraphAccess(access.flags, required)) {
				return;
			}
		}
		throw std::invalid_argument(message);
	}

	void ValidateTextureRegion(TextureRegion const& region, char const* message) {
		if (region.width == 0u || region.height == 0u || region.depth == 0u ||
			region.array_layer_count == 0u) {
			throw std::invalid_argument(message);
		}
	}

	void ValidateTextureDataLayout(TextureDataLayout const& layout, char const* message) {
		if (layout.bytes_per_row == 0u || layout.rows_per_image == 0u) {
			throw std::invalid_argument(message);
		}
	}

	struct ValidateGraphCommand {
		CommandGraphDescriptor const& descriptor;
		GraphNodeDescriptor const& node;

		void ValidateCopyResources(GraphResourceID source, GraphResourceID destination) const {
			ValidateNodeCapability(node, GraphNodeFlagBits::Copy,
				"Copy command requires a copy node");
			ValidateResourceID(descriptor, source);
			ValidateResourceID(descriptor, destination);
			ValidateResourceAccess(node, source,
				GraphAccessFlagBits::Read | GraphAccessFlagBits::CopySource,
				"Copy source is missing its read copy-source access");
			ValidateResourceAccess(node, destination,
				GraphAccessFlagBits::Write | GraphAccessFlagBits::CopyDestination,
				"Copy destination is missing its write copy-destination access");
		}

		void operator()(BeginRenderingCommand const& command) const {
			ValidateNodeCapability(node, GraphNodeFlagBits::Graphics,
				"BeginRendering requires a graphics node");
			if (command.width == 0u || command.height == 0u ||
				command.offset_x < 0 || command.offset_y < 0) {
				throw std::invalid_argument("BeginRendering contains an invalid rendering area");
			}
			for (auto const& color : command.colors) {
				ValidateResourceID(descriptor, color.resource);
				ValidateViewID(descriptor, color.view);
				ValidateResourceAccess(node, color.resource,
					GraphAccessFlagBits::Write | GraphAccessFlagBits::ColorAttachment,
					"Color attachment is missing its write attachment access");
				if (color.resolve_resource.has_value() != color.resolve_view.has_value()) {
					throw std::invalid_argument("Resolve attachment requires both resource and view IDs");
				}
				if (color.resolve_resource) {
					ValidateResourceID(descriptor, *color.resolve_resource);
					ValidateViewID(descriptor, *color.resolve_view);
					ValidateResourceAccess(node, color.resource,
						GraphAccessFlagBits::Read | GraphAccessFlagBits::ResolveSource,
						"Resolve source is missing its read resolve access");
					ValidateResourceAccess(node, *color.resolve_resource,
						GraphAccessFlagBits::Write | GraphAccessFlagBits::ResolveDestination,
						"Resolve destination is missing its write resolve access");
				}
			}
			if (command.depth_stencil) {
				auto const& depth = *command.depth_stencil;
				ValidateResourceID(descriptor, depth.resource);
				ValidateViewID(descriptor, depth.view);
				auto required = GraphAccessFlagBits::DepthStencilAttachment |
					(depth.load_depth || depth.load_stencil
						? GraphAccessFlagBits::Read : GraphAccessFlagBits::None) |
					(!depth.store_depth && !depth.store_stencil
						? GraphAccessFlagBits::None : GraphAccessFlagBits::Write);
				ValidateResourceAccess(node, depth.resource, required,
					"Depth/stencil attachment is missing its declared attachment access");
			}
		}

		void operator()(EndRenderingCommand const&) const {
			ValidateNodeCapability(node, GraphNodeFlagBits::Graphics,
				"EndRendering requires a graphics node");
		}

		void operator()(BindPipelineCommand const& command) const {
			if (command.pipeline.value >= descriptor.pipeline_count) {
				throw std::out_of_range("BindPipeline contains an invalid pipeline ID");
			}
			if (!HasGraphFlags(node.flags, GraphNodeFlagBits::Graphics) &&
				!HasGraphFlags(node.flags, GraphNodeFlagBits::Compute)) {
				throw std::invalid_argument("BindPipeline requires a graphics or compute node");
			}
		}

		void operator()(BindResourceGroupCommand const& command) const {
			if (command.group.value >= descriptor.resource_group_count) {
				throw std::out_of_range("BindResourceGroup contains an invalid resource group ID");
			}
			if (!HasGraphFlags(node.flags, GraphNodeFlagBits::Graphics) &&
				!HasGraphFlags(node.flags, GraphNodeFlagBits::Compute)) {
				throw std::invalid_argument("BindResourceGroup requires a graphics or compute node");
			}
		}

		void operator()(BindVertexBufferCommand const& command) const {
			ValidateNodeCapability(node, GraphNodeFlagBits::Graphics,
				"BindVertexBuffer requires a graphics node");
			ValidateResourceID(descriptor, command.resource);
			ValidateResourceAccess(node, command.resource,
				GraphAccessFlagBits::Read | GraphAccessFlagBits::Vertex,
				"Vertex buffer is missing its read vertex access");
		}

		void operator()(BindIndexBufferCommand const& command) const {
			ValidateNodeCapability(node, GraphNodeFlagBits::Graphics,
				"BindIndexBuffer requires a graphics node");
			ValidateResourceID(descriptor, command.resource);
			ValidateResourceAccess(node, command.resource,
				GraphAccessFlagBits::Read | GraphAccessFlagBits::Index,
				"Index buffer is missing its read index access");
		}

		void operator()(SetViewportCommand const& command) const {
			ValidateNodeCapability(node, GraphNodeFlagBits::Graphics,
				"SetViewport requires a graphics node");
			if (command.width < 0.0f || command.height < 0.0f ||
				command.minimum_depth < 0.0f || command.maximum_depth > 1.0f ||
				command.minimum_depth > command.maximum_depth) {
				throw std::invalid_argument("SetViewport contains an invalid viewport");
			}
		}

		void operator()(SetScissorCommand const& command) const {
			ValidateNodeCapability(node, GraphNodeFlagBits::Graphics,
				"SetScissor requires a graphics node");
			if (command.x < 0 || command.y < 0) {
				throw std::invalid_argument("SetScissor contains a negative offset");
			}
		}

		void operator()(DrawCommand const&) const {
			ValidateNodeCapability(node, GraphNodeFlagBits::Graphics,
				"Draw requires a graphics node");
		}

		void operator()(DrawIndexedCommand const&) const {
			ValidateNodeCapability(node, GraphNodeFlagBits::Graphics,
				"DrawIndexed requires a graphics node");
		}

		void operator()(DispatchCommand const& command) const {
			ValidateNodeCapability(node, GraphNodeFlagBits::Compute,
				"Dispatch requires a compute node");
			if (command.group_count_x == 0u || command.group_count_y == 0u ||
				command.group_count_z == 0u) {
				throw std::invalid_argument("Dispatch group counts must be non-zero");
			}
		}

		void operator()(CopyBufferCommand const& command) const {
			ValidateCopyResources(command.source, command.destination);
			if (command.size == 0u) {
				throw std::invalid_argument("CopyBuffer size must be non-zero");
			}
		}

		void operator()(CopyBufferToTextureCommand const& command) const {
			ValidateCopyResources(command.source, command.destination);
			ValidateTextureDataLayout(command.source_layout,
				"CopyBufferToTexture contains an invalid buffer layout");
			ValidateTextureRegion(command.destination_region,
				"CopyBufferToTexture contains an invalid texture region");
		}

		void operator()(CopyTextureToBufferCommand const& command) const {
			ValidateCopyResources(command.source, command.destination);
			ValidateTextureRegion(command.source_region,
				"CopyTextureToBuffer contains an invalid texture region");
			ValidateTextureDataLayout(command.destination_layout,
				"CopyTextureToBuffer contains an invalid buffer layout");
		}

		void operator()(CopyTextureCommand const& command) const {
			ValidateCopyResources(command.source, command.destination);
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

		void operator()(PresentCommand const& command) const {
			ValidateNodeCapability(node, GraphNodeFlagBits::Present,
				"Present requires a presentation node");
			ValidateResourceID(descriptor, command.source);
			if (command.target.value >= descriptor.presentation_target_count) {
				throw std::out_of_range("Present contains an invalid presentation target ID");
			}
			if (command.frames_in_flight < 2u) {
				throw std::invalid_argument("Present requires at least two frames in flight");
			}
			ValidateResourceAccess(node, command.source,
				GraphAccessFlagBits::Read | GraphAccessFlagBits::Present,
				"Presentation source is missing its read presentation access");
		}
	};

	void ValidateCommandGraphDescriptor(CommandGraphDescriptor const& descriptor) {
		for (std::size_t index = 0u; index < descriptor.nodes.size(); ++index) {
			auto const& node = descriptor.nodes[index];
			if (node.id.value != index) {
				throw std::invalid_argument("Command graph node IDs must match their descriptor indices");
			}
			if (node.flags == GraphNodeFlagBits::None) {
				throw std::invalid_argument("Command graph node capability must not be empty");
			}
			for (auto dependency : node.dependencies) {
				if (dependency.value >= descriptor.nodes.size() || dependency == node.id) {
					throw std::invalid_argument("Command graph contains an invalid dependency");
				}
			}
			for (auto const& access : node.accesses) {
				ValidateResourceID(descriptor, access.resource);
				if (!HasGraphAccess(access.flags, GraphAccessFlagBits::Read) &&
					!HasGraphAccess(access.flags, GraphAccessFlagBits::Write)) {
					throw std::invalid_argument("Command graph access must contain Read or Write");
				}
			}
			for (auto const& command : node.commands) {
				std::visit(ValidateGraphCommand{ descriptor, node }, command);
			}
		}
	}

}
