module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>
#endif // !defined(__cpp_lib_modules)

export module fyuu_rhi:command_graph_types;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace fyuu_rhi::execution {

	export struct GraphNodeID {
		std::uint32_t value;

		std::strong_ordering operator<=>(GraphNodeID const&) const noexcept = default;
	};

	export struct GraphResourceID {
		std::uint32_t value;

		std::strong_ordering operator<=>(GraphResourceID const&) const noexcept = default;
	};

	export struct GraphPipelineID {
		std::uint32_t value;

		std::strong_ordering operator<=>(GraphPipelineID const&) const noexcept = default;
	};

	export struct GraphViewID {
		std::uint32_t value;

		std::strong_ordering operator<=>(GraphViewID const&) const noexcept = default;
	};

	export struct GraphResourceGroupID {
		std::uint32_t value;

		std::strong_ordering operator<=>(GraphResourceGroupID const&) const noexcept = default;
	};

	export struct GraphPresentationID {
		std::uint32_t value;

		std::strong_ordering operator<=>(GraphPresentationID const&) const noexcept = default;
	};

	export enum class GraphNodeFlagBits : std::uint8_t {
		None = 0u,
		Graphics = 1u << 0u,
		Compute = 1u << 1u,
		Copy = 1u << 2u,
		Present = 1u << 3u
	};

	export constexpr GraphNodeFlagBits operator|(GraphNodeFlagBits lhs, GraphNodeFlagBits rhs) noexcept {
		return static_cast<GraphNodeFlagBits>(
			static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs)
		);
	}

	export constexpr GraphNodeFlagBits operator&(GraphNodeFlagBits lhs, GraphNodeFlagBits rhs) noexcept {
		return static_cast<GraphNodeFlagBits>(
			static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs)
		);
	}

	export constexpr GraphNodeFlagBits& operator|=(GraphNodeFlagBits& lhs, GraphNodeFlagBits rhs) noexcept {
		return lhs = lhs | rhs;
	}

	export enum class GraphAccessFlagBits : std::uint32_t {
		None = 0u,
		Read = 1u << 0u,
		Write = 1u << 1u,
		Indirect = 1u << 2u,
		Vertex = 1u << 3u,
		Index = 1u << 4u,
		Uniform = 1u << 5u,
		Storage = 1u << 6u,
		Sampled = 1u << 7u,
		ColorAttachment = 1u << 8u,
		DepthStencilAttachment = 1u << 9u,
		CopySource = 1u << 10u,
		CopyDestination = 1u << 11u,
		ResolveSource = 1u << 12u,
		ResolveDestination = 1u << 13u,
		Present = 1u << 14u
	};

	export constexpr GraphAccessFlagBits operator|(GraphAccessFlagBits lhs, GraphAccessFlagBits rhs) noexcept {
		return static_cast<GraphAccessFlagBits>(
			static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs)
		);
	}

	export constexpr GraphAccessFlagBits operator&(GraphAccessFlagBits lhs, GraphAccessFlagBits rhs) noexcept {
		return static_cast<GraphAccessFlagBits>(
			static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs)
		);
	}

	export constexpr GraphAccessFlagBits& operator|=(GraphAccessFlagBits& lhs, GraphAccessFlagBits rhs) noexcept {
		return lhs = lhs | rhs;
	}

	export struct GraphSubresourceRange {
		std::uint32_t base_mip_level = 0u;
		std::uint32_t mip_level_count = 0u;
		std::uint32_t base_array_layer = 0u;
		std::uint32_t array_layer_count = 0u;
		std::size_t offset = 0u;
		std::size_t size = 0u;
	};

	export struct GraphResourceAccess {
		GraphResourceID resource;
		GraphAccessFlagBits flags;
		GraphSubresourceRange range;
	};

	export struct GraphColorAttachment {
		GraphResourceID resource;
		GraphViewID view;
		std::optional<GraphResourceID> resolve_resource;
		std::optional<GraphViewID> resolve_view;
		bool load = false;
		bool store = true;
		float clear_red = 0.0f;
		float clear_green = 0.0f;
		float clear_blue = 0.0f;
		float clear_alpha = 0.0f;
	};

	export struct GraphDepthStencilAttachment {
		GraphResourceID resource;
		GraphViewID view;
		bool load_depth = false;
		bool store_depth = true;
		bool load_stencil = false;
		bool store_stencil = true;
		float clear_depth = 1.0f;
		std::uint32_t clear_stencil = 0u;
	};

	export struct BeginRenderingCommand {
		std::vector<GraphColorAttachment> colors;
		std::optional<GraphDepthStencilAttachment> depth_stencil;
		std::int32_t offset_x = 0;
		std::int32_t offset_y = 0;
		std::uint32_t width = 0u;
		std::uint32_t height = 0u;
	};

	export struct EndRenderingCommand {};

	export struct BindPipelineCommand {
		GraphPipelineID pipeline;
	};

	export struct BindResourceGroupCommand {
		GraphResourceGroupID group;
		std::uint32_t index = 0u;
	};

	export struct BindVertexBufferCommand {
		GraphResourceID resource;
		std::uint32_t slot = 0u;
		std::uint32_t stride = 0u;
		std::size_t offset = 0u;
	};

	export struct SetViewportCommand {
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		float minimum_depth = 0.0f;
		float maximum_depth = 1.0f;
	};

	export struct SetScissorCommand {
		std::int32_t x = 0;
		std::int32_t y = 0;
		std::uint32_t width = 0u;
		std::uint32_t height = 0u;
	};

	export struct BindIndexBufferCommand {
		GraphResourceID resource;
		std::size_t offset = 0u;
		bool uint32 = false;
	};

	export struct DrawCommand {
		std::uint32_t vertex_count = 0u;
		std::uint32_t instance_count = 1u;
		std::uint32_t first_vertex = 0u;
		std::uint32_t first_instance = 0u;
	};

	export struct DrawIndexedCommand {
		std::uint32_t index_count = 0u;
		std::uint32_t instance_count = 1u;
		std::uint32_t first_index = 0u;
		std::int32_t vertex_offset = 0;
		std::uint32_t first_instance = 0u;
	};

	export struct DispatchCommand {
		std::uint32_t group_count_x = 1u;
		std::uint32_t group_count_y = 1u;
		std::uint32_t group_count_z = 1u;
	};

	export struct CopyBufferCommand {
		GraphResourceID source;
		GraphResourceID destination;
		std::size_t source_offset = 0u;
		std::size_t destination_offset = 0u;
		std::size_t size = 0u;
	};

	export struct PresentCommand {
		GraphResourceID source;
		GraphPresentationID target;
		bool vertical_sync = true;
		std::uint32_t frames_in_flight = 3u;
	};

	export using GraphCommand = std::variant<
		BeginRenderingCommand,
		EndRenderingCommand,
		BindPipelineCommand,
		BindResourceGroupCommand,
		BindVertexBufferCommand,
		BindIndexBufferCommand,
		SetViewportCommand,
		SetScissorCommand,
		DrawCommand,
		DrawIndexedCommand,
		DispatchCommand,
		CopyBufferCommand,
		PresentCommand
	>;

	export struct GraphNodeDescriptor {
		GraphNodeID id;
		GraphNodeFlagBits flags;
		std::vector<GraphNodeID> dependencies;
		std::vector<GraphResourceAccess> accesses;
		std::vector<GraphCommand> commands;
	};

	export struct CommandGraphDescriptor {
		std::vector<GraphNodeDescriptor> nodes;
		std::uint32_t resource_count = 0u;
		std::uint32_t pipeline_count = 0u;
		std::uint32_t view_count = 0u;
		std::uint32_t resource_group_count = 0u;
		std::uint32_t presentation_target_count = 0u;
	};

}
