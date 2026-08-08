module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <vector>

#include <cstdint>
#include <type_traits>

#include <optional>
#include <variant>

#include <concepts>
#endif // !defined(__cpp_lib_modules)

export module fyuu_rhi:execution_types;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core_types;
import :resource_types;

namespace fyuu_rhi {
	template <class Backend> class Resource;
	template <class Backend> class View;
	template <class Backend> class Sampler;

	namespace pipeline {
		template <class Backend> class Pipeline;
		template <class Backend> class PipelineResourceGroup;
	}
}

namespace fyuu_rhi::execution {
	export class StopTokenView {
		void const* m_token = nullptr;
		bool (*m_stop_requested)(void const*) noexcept = nullptr;

		template <class StopToken>
		[[nodiscard]] static bool StopRequested(void const* token) noexcept {
			return static_cast<StopToken const*>(token)->stop_requested();
		}

	public:
		StopTokenView() noexcept = default;

		template <class StopToken>
			requires requires(StopToken const& token) {
				{ token.stop_requested() } noexcept -> std::convertible_to<bool>;
			}
		StopTokenView(StopToken const& token) noexcept
			: m_token(&token),
			m_stop_requested(&StopRequested<StopToken>) {

		}

		[[nodiscard]] bool stop_requested() const noexcept {
			return m_stop_requested && m_stop_requested(m_token);
		}
	};

	export enum class QueueType : std::uint8_t {
		Graphics,
		Compute,
		Transfer,
		Present
	};

	export enum class AccessMode : std::uint8_t {
		Read,
		Write,
		ReadWrite
	};

	export enum class ResourceUsage : std::uint32_t {
		None = 0u,
		Indirect = 1u << 0u,
		VertexBuffer = 1u << 1u,
		IndexBuffer = 1u << 2u,
		Uniform = 1u << 3u,
		Storage = 1u << 4u,
		Sampled = 1u << 5u,
		ColorAttachment = 1u << 6u,
		DepthStencilAttachment = 1u << 7u,
		CopySource = 1u << 8u,
		CopyDestination = 1u << 9u,
		ResolveSource = 1u << 10u,
		ResolveDestination = 1u << 11u,
		PresentationSource = 1u << 12u
	};

	export constexpr ResourceUsage operator|(ResourceUsage lhs, ResourceUsage rhs) noexcept {
		return static_cast<ResourceUsage>(
			static_cast<std::underlying_type_t<ResourceUsage>>(lhs) |
			static_cast<std::underlying_type_t<ResourceUsage>>(rhs)
		);
	}

	export constexpr ResourceUsage operator&(ResourceUsage lhs, ResourceUsage rhs) noexcept {
		return static_cast<ResourceUsage>(
			static_cast<std::underlying_type_t<ResourceUsage>>(lhs) &
			static_cast<std::underlying_type_t<ResourceUsage>>(rhs)
		);
	}

	export constexpr ResourceUsage& operator|=(ResourceUsage& lhs, ResourceUsage rhs) noexcept {
		return lhs = lhs | rhs;
	}

	export [[nodiscard]] constexpr bool HasUsage(ResourceUsage value, ResourceUsage expected) noexcept {
		return (value & expected) == expected;
	}

	export struct BufferRange {
		std::size_t offset = 0u;
		std::size_t size = 0u;
	};

	export struct TextureRange {
		std::uint32_t base_mip_level = 0u;
		std::uint32_t mip_level_count = 0u;
		std::uint32_t base_array_layer = 0u;
		std::uint32_t array_layer_count = 0u;
	};

	// monostate means the complete resource. A zero count in a concrete range
	// is invalid rather than another spelling of the complete resource.
	export using ResourceRange = std::variant<std::monostate, BufferRange, TextureRange>;

	export struct ResourceAccess {
		std::size_t resource;
		AccessMode mode = AccessMode::Read;
		ResourceUsage usage = ResourceUsage::None;
		ResourceRange range;
	};

	export enum class LoadOperation : std::uint8_t {
		Load,
		Clear,
		Discard
	};

	export enum class StoreOperation : std::uint8_t {
		Store,
		Discard
	};

	export struct ColorClearValue {
		float red = 0.0f;
		float green = 0.0f;
		float blue = 0.0f;
		float alpha = 0.0f;
	};

	export struct ColorAttachment {
		std::size_t resource;
		std::size_t view;
		LoadOperation load = LoadOperation::Clear;
		StoreOperation store = StoreOperation::Store;
		ColorClearValue clear;
		std::optional<std::size_t> resolve_resource;
		std::optional<std::size_t> resolve_view;
	};

	export struct DepthStencilAttachment {
		std::size_t resource;
		std::size_t view;
		LoadOperation depth_load = LoadOperation::Clear;
		StoreOperation depth_store = StoreOperation::Store;
		LoadOperation stencil_load = LoadOperation::Discard;
		StoreOperation stencil_store = StoreOperation::Discard;
		float clear_depth = 1.0f;
		std::uint32_t clear_stencil = 0u;
	};

	export struct RenderArea {
		std::int32_t x = 0;
		std::int32_t y = 0;
		std::uint32_t width = 0u;
		std::uint32_t height = 0u;
	};

	export struct BeginRendering {
		RenderArea area;
		std::vector<ColorAttachment> colors;
		std::optional<DepthStencilAttachment> depth_stencil;
	};

	export struct EndRendering {};

	export struct BindPipeline {
		std::size_t pipeline;
	};

	export struct BindResourceGroup {
		std::size_t group;
		std::uint32_t index = 0u;
	};

	export struct BindVertexBuffer {
		std::size_t resource;
		std::uint32_t slot = 0u;
		std::uint32_t stride = 0u;
		std::size_t offset = 0u;
	};

	export enum class IndexType : std::uint8_t {
		Uint16,
		Uint32
	};

	export struct BindIndexBuffer {
		std::size_t resource;
		IndexType type = IndexType::Uint16;
		std::size_t offset = 0u;
	};

	export struct Viewport {
		float x = 0.0f;
		float y = 0.0f;
		float width = 0.0f;
		float height = 0.0f;
		float minimum_depth = 0.0f;
		float maximum_depth = 1.0f;
		/// NDC Y orientation the application authors its clip space in. Backends
		/// whose native convention differs (Vulkan is Y-down) flip the viewport to
		/// match. D3D12/WebGPU/OpenGL are natively Y-up, so both values coincide.
		ClipSpace clip_space = ClipSpace::YUp;
	};

	export struct Scissor {
		std::int32_t x = 0;
		std::int32_t y = 0;
		std::uint32_t width = 0u;
		std::uint32_t height = 0u;
	};

	export struct Draw {
		std::uint32_t vertex_count = 0u;
		std::uint32_t instance_count = 1u;
		std::uint32_t first_vertex = 0u;
		std::uint32_t first_instance = 0u;
	};

	export struct DrawIndexed {
		std::uint32_t index_count = 0u;
		std::uint32_t instance_count = 1u;
		std::uint32_t first_index = 0u;
		std::int32_t vertex_offset = 0;
		std::uint32_t first_instance = 0u;
	};

	export struct Dispatch {
		std::uint32_t group_count_x = 1u;
		std::uint32_t group_count_y = 1u;
		std::uint32_t group_count_z = 1u;
	};

	export struct CopyBuffer {
		std::size_t source;
		std::size_t destination;
		std::size_t source_offset = 0u;
		std::size_t destination_offset = 0u;
		std::size_t size = 0u;
	};

	export struct CopyBufferToTexture {
		std::size_t source;
		std::size_t destination;
		TextureDataLayout source_layout;
		TextureRegion destination_region;
	};

	export struct CopyTextureToBuffer {
		std::size_t source;
		std::size_t destination;
		TextureRegion source_region;
		TextureDataLayout destination_layout;
	};

	export struct CopyTexture {
		std::size_t source;
		std::size_t destination;
		TextureRegion source_region;
		TextureRegion destination_region;
	};

	/// Writes host data into a bound buffer. Backends implement this as a CPU
	/// mapping (D3D12 Map / Vulkan vmaMapMemory / OpenGL glNamedBufferSubData /
	/// WebGPU MapAsync) during replay, so the target must be host-visible. Used as
	/// the write half of a CPU->GPU upload paired with a CopyBuffer/CopyBufferToTexture.
	export struct WriteBuffer {
		std::size_t resource;
		std::size_t offset = 0u;
		std::vector<std::byte> data;
	};

	export struct Present {
		std::size_t source;
		std::size_t target = 0u;
		std::uint32_t buffer_count = 3u;
		bool vertical_sync = true;
	};

	export using CommandRecord = std::variant<
		BeginRendering,
		EndRendering,
		BindPipeline,
		BindResourceGroup,
		BindVertexBuffer,
		BindIndexBuffer,
		Viewport,
		Scissor,
		Draw,
		DrawIndexed,
		Dispatch,
		CopyBuffer,
		CopyBufferToTexture,
		CopyTextureToBuffer,
		CopyTexture,
		WriteBuffer,
		Present
	>;

	export struct ExecutionNode {
		std::size_t id;
		QueueType queue = QueueType::Graphics;
		std::vector<std::size_t> dependencies;
		std::vector<ResourceAccess> accesses;
		std::vector<CommandRecord> commands;
	};

	export struct BindingLayout {
		std::uint32_t resource_count = 0u;
		std::uint32_t view_count = 0u;
		std::uint32_t sampler_count = 0u;
		std::uint32_t pipeline_count = 0u;
		std::uint32_t resource_group_count = 0u;
	};

	export struct ExecutionGraph {
		BindingLayout bindings;
		std::vector<ExecutionNode> nodes;
	};

	export struct ExecutionBarrier {
		std::size_t resource;
		std::size_t source_node;
		std::size_t destination_node;
		std::size_t source_batch;
		std::size_t destination_batch;
		QueueType source_queue;
		QueueType destination_queue;
		AccessMode source_mode;
		AccessMode destination_mode;
		ResourceUsage source_usage;
		ResourceUsage destination_usage;
		ResourceRange source_range;
		ResourceRange destination_range;

		[[nodiscard]] bool CrossQueue() const noexcept {
			return source_queue != destination_queue;
		}
	};

	export struct ExecutionBoundaryAccess {
		std::size_t resource;
		std::size_t node;
		std::size_t batch;
		QueueType queue;
		AccessMode mode;
		ResourceUsage usage;
		ResourceRange range;
	};

	export struct ExecutionBatch {
		std::size_t id;
		QueueType queue;
		std::vector<ExecutionNode> nodes;
		std::vector<std::size_t> dependencies;
		std::vector<ExecutionBarrier> release_barriers;
		std::vector<ExecutionBarrier> barriers;
	};

	export struct ExecutionPlan {
		BindingLayout bindings;
		std::vector<std::size_t> topological_order;
		std::vector<std::size_t> node_batches;
		std::vector<ExecutionBatch> batches;
		std::vector<std::vector<ExecutionBoundaryAccess>> first_accesses;
		std::vector<std::vector<ExecutionBoundaryAccess>> last_accesses;
	};

} // namespace fyuu_rhi::execution
