module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

#include <deque>
#include <map>
#include <set>
#include <vector>

#include <algorithm>
#include <functional>
#include <limits>

#include <cstdint>
#include <type_traits>

#include <array>
#include <unordered_map>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <optional>
#include <variant>

#include <execution>

#include <compare>
#include <concepts>

#include <span>

#include <stop_token>

#endif // !defined(__cpp_lib_modules)

#if defined(_WIN32)
#include <Windows.h>
#elif defined(__ANDROID__)
#include <android/native_window.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <wayland-client-core.h>
#elif defined(__APPLE__)
#include <QuartzCore/CAMetalLayer.hpp>
#endif // defined(_WIN32)

export module fyuu_rhi:execution;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource;
import :view;
import :sampler;
import :pipeline;

export namespace fyuu_rhi::execution {

	/// The clip-space convention the application authors its content in. The RHI
	/// normalizes each backend to the requested orientation so shaders are portable.
	/// Handedness (left/right-handed) is expressed by the app's projection matrix
	/// and is not part of rasterization, so it is not represented here.
	enum class ClipSpace : std::uint8_t {
		/// NDC +Y points toward the top of the framebuffer (D3D12, WebGPU and OpenGL
		/// convention). Vulkan backends compensate with a flipped viewport.
		YUp,
		/// Use each backend's native NDC (Vulkan: +Y down; D3D12/WebGPU/GL: +Y up).
		ApiNative
	};

	/// Capability requested by a graph node; it does not identify a physical queue.
	enum class QueueType : std::uint8_t { Graphics, Compute, Transfer, Present };

	/// Whether a node reads, writes, or both reads and writes a declared range.
	enum class AccessMode : std::uint8_t { Read, Write, ReadWrite };

	/// Command-time resource role used to derive states, barriers, and synchronization.
	enum class ResourceUsage : std::uint32_t {
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

	constexpr ResourceUsage operator|(ResourceUsage lhs, ResourceUsage rhs) noexcept {
		return static_cast<ResourceUsage>(
		    static_cast<std::underlying_type_t<ResourceUsage>>(lhs) |
		    static_cast<std::underlying_type_t<ResourceUsage>>(rhs)
		);
	}

	constexpr ResourceUsage operator&(ResourceUsage lhs, ResourceUsage rhs) noexcept {
		return static_cast<ResourceUsage>(
		    static_cast<std::underlying_type_t<ResourceUsage>>(lhs) &
		    static_cast<std::underlying_type_t<ResourceUsage>>(rhs)
		);
	}

	constexpr ResourceUsage& operator|=(ResourceUsage& lhs, ResourceUsage rhs) noexcept {
		return lhs = lhs | rhs;
	}

	constexpr bool HasUsage(ResourceUsage value, ResourceUsage expected) noexcept {
		return (value & expected) == expected;
	}

	/// Non-owning type-erased view of any noexcept stop token.
	class StopTokenView {
	private:
		void const* m_token = nullptr;
		bool (*m_stop_requested)(void const*) noexcept = nullptr;

		template <class StopToken> static bool StopRequested(void const* token) noexcept {
			return static_cast<StopToken const*>(token)->stop_requested();
		}

	public:
		StopTokenView() noexcept = default;

		template <class StopToken>
		    requires requires(StopToken const& token) {
			    { token.stop_requested() } noexcept -> std::convertible_to<bool>;
		    }
		StopTokenView(StopToken const& token) noexcept :
		    m_token(&token), m_stop_requested(&StopRequested<StopToken>) {
		}

		bool stop_requested() const noexcept {
			return m_stop_requested && m_stop_requested(m_token);
		}
	};

	struct BufferRange {
		std::size_t offset = 0u;
		std::size_t size = 0u;
	};

	struct TextureRange {
		std::uint32_t base_mip_level = 0u;
		std::uint32_t mip_level_count = 0u;
		std::uint32_t base_array_layer = 0u;
		std::uint32_t array_layer_count = 0u;
	};

	// monostate means the complete resource. A zero count in a concrete range
	// is invalid rather than another spelling of the complete resource.
	using ResourceRange = std::variant<std::monostate, BufferRange, TextureRange>;

	/**
	 * @brief Declares one node's access to a registered resource range.
	 *
	 * This is a correctness contract rather than a hint. Commands in the node must
	 * agree with it; the scheduler uses it to build barriers and queue dependencies.
	 */
	struct ResourceAccess {
		std::size_t resource;
		AccessMode mode = AccessMode::Read;
		ResourceUsage usage = ResourceUsage::None;
		ResourceRange range;
	};

	enum class LoadOperation : std::uint8_t { Load, Clear, Discard };

	enum class StoreOperation : std::uint8_t { Store, Discard };

	struct ColorClearValue {
		float red = 0.0f;
		float green = 0.0f;
		float blue = 0.0f;
		float alpha = 0.0f;
	};

	struct ColorAttachment {
		std::size_t resource;
		std::size_t view;
		LoadOperation load = LoadOperation::Clear;
		StoreOperation store = StoreOperation::Store;
		ColorClearValue clear;
		std::optional<std::size_t> resolve_resource;
		std::optional<std::size_t> resolve_view;
	};

	struct DepthStencilAttachment {
		std::size_t resource;
		std::size_t view;
		LoadOperation depth_load = LoadOperation::Clear;
		StoreOperation depth_store = StoreOperation::Store;
		LoadOperation stencil_load = LoadOperation::Discard;
		StoreOperation stencil_store = StoreOperation::Discard;
		float clear_depth = 1.0f;
		std::uint32_t clear_stencil = 0u;
	};

	struct RenderArea {
		std::int32_t x = 0;
		std::int32_t y = 0;
		std::uint32_t width = 0u;
		std::uint32_t height = 0u;
	};

	/// Begins a render scope with color and optional depth/stencil attachments.
	struct BeginRendering {
		RenderArea area;
		std::vector<ColorAttachment> colors;
		std::optional<DepthStencilAttachment> depth_stencil;
	};

	/// Ends the active render scope.
	struct EndRendering {};

	/// Binds a registered graphics or compute pipeline.
	struct BindPipeline {
		std::size_t pipeline;
	};

	/** @brief Binds an immutable resource group and optional per-draw buffer offsets. */
	struct BindResourceGroup {
		std::size_t group;
		/// Logical descriptor space/set occupied by the resource group.
		std::uint32_t space = 0u;
		/// Additional byte offsets for buffer bindings, ordered by slot and then
		/// array element. Texture and sampler bindings do not consume an entry.
		/// An empty array keeps every buffer at its resource-group base offset.
		std::vector<std::size_t> additional_buffer_offsets;
	};

	/// Updates one reflected immediate-constant range of the currently bound
	/// graphics or compute pipeline. Offset is relative to the beginning of the
	/// range identified by slot and space. Data and offset are four-byte aligned
	/// so every backend observes the same write granularity.
	struct SetPipelineConstants {
		std::uint32_t slot = 0u;
		std::uint32_t space = 0u;
		std::uint32_t offset = 0u;
		std::vector<std::byte> data;
	};

	/// Binds a registered buffer as one vertex-input slot.
	struct BindVertexBuffer {
		std::size_t resource;
		std::uint32_t slot = 0u;
		std::uint32_t stride = 0u;
		std::size_t offset = 0u;
	};

	enum class IndexType : std::uint8_t { Uint16, Uint32 };

	/// Binds a registered buffer as the index source for subsequent indexed draws.
	struct BindIndexBuffer {
		std::size_t resource;
		IndexType type = IndexType::Uint16;
		std::size_t offset = 0u;
	};

	/// Sets viewport bounds, depth range, and application clip-space orientation.
	struct Viewport {
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

	/// Restricts rasterization to a framebuffer rectangle.
	struct Scissor {
		std::int32_t x = 0;
		std::int32_t y = 0;
		std::uint32_t width = 0u;
		std::uint32_t height = 0u;
	};

	/// Issues a non-indexed instanced draw.
	struct Draw {
		std::uint32_t vertex_count = 0u;
		std::uint32_t instance_count = 1u;
		std::uint32_t first_vertex = 0u;
		std::uint32_t first_instance = 0u;
	};

	/// Issues an indexed instanced draw using the current index buffer.
	struct DrawIndexed {
		std::uint32_t index_count = 0u;
		std::uint32_t instance_count = 1u;
		std::uint32_t first_index = 0u;
		std::int32_t vertex_offset = 0;
		std::uint32_t first_instance = 0u;
	};

	/// Dispatches a three-dimensional compute workgroup grid.
	struct Dispatch {
		std::uint32_t group_count_x = 1u;
		std::uint32_t group_count_y = 1u;
		std::uint32_t group_count_z = 1u;
	};

	/// Copies a byte range between two registered buffers.
	struct CopyBuffer {
		std::size_t source;
		std::size_t destination;
		std::size_t source_offset = 0u;
		std::size_t destination_offset = 0u;
		std::size_t size = 0u;
	};

	/// Copies formatted buffer data into a texture region.
	struct CopyBufferToTexture {
		std::size_t source;
		std::size_t destination;
		TextureDataLayout source_layout;
		TextureRegion destination_region;
	};

	/// Copies a texture region into a formatted buffer layout.
	struct CopyTextureToBuffer {
		std::size_t source;
		std::size_t destination;
		TextureRegion source_region;
		TextureDataLayout destination_layout;
	};

	/// Copies matching regions between two textures.
	struct CopyTexture {
		std::size_t source;
		std::size_t destination;
		TextureRegion source_region;
		TextureRegion destination_region;
	};

	/// Writes host data into a bound buffer. Backends implement this as a CPU
	/// mapping (D3D12 Map / Vulkan vmaMapMemory / OpenGL glNamedBufferSubData /
	/// WebGPU MapAsync) during replay, so the target must be host-visible. Used as
	/// the write half of a CPU->GPU upload paired with a CopyBuffer/CopyBufferToTexture.
	struct WriteBuffer {
		std::size_t resource;
		std::size_t offset = 0u;
		std::vector<std::byte> data;
	};

	/// Presents a registered texture to a registered native-window target.
	struct Present {
		std::size_t source;
		std::size_t target = 0u;
		std::uint32_t buffer_count = 3u;
		bool vertical_sync = true;
	};

	using CommandRecord = std::variant<
	    BeginRendering,
	    EndRendering,
	    BindPipeline,
	    BindResourceGroup,
	    SetPipelineConstants,
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
	    Present>;

	/// User-authored graph node before validation, batching, and barrier planning.
	struct ExecutionNode {
		std::size_t id;
		QueueType queue = QueueType::Graphics;
		std::vector<std::size_t> dependencies;
		std::vector<ResourceAccess> accesses;
		std::vector<CommandRecord> commands;
	};

	struct BindingLayout {
		std::uint32_t resource_count = 0u;
		std::uint32_t view_count = 0u;
		std::uint32_t sampler_count = 0u;
		std::uint32_t pipeline_count = 0u;
		std::uint32_t resource_group_count = 0u;
	};

	struct ExecutionGraph {
		BindingLayout bindings;
		std::vector<ExecutionNode> nodes;
	};

	struct ExecutionBarrier {
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

		bool CrossQueue() const noexcept {
			return source_queue != destination_queue;
		}
	};

	struct ExecutionBoundaryAccess {
		std::size_t resource;
		std::size_t node;
		std::size_t batch;
		QueueType queue;
		AccessMode mode;
		ResourceUsage usage;
		ResourceRange range;
	};

	struct ExecutionBatch {
		std::size_t id;
		QueueType queue;
		std::vector<ExecutionNode> nodes;
		std::vector<std::size_t> dependencies;
		std::vector<ExecutionBarrier> release_barriers;
		std::vector<ExecutionBarrier> barriers;
	};

	/// Validated, topologically sorted and batched graph consumed by a backend.
	struct ExecutionPlan {
		BindingLayout bindings;
		std::vector<std::size_t> topological_order;
		std::vector<std::size_t> node_batches;
		std::vector<ExecutionBatch> batches;
		std::vector<std::vector<ExecutionBoundaryAccess>> first_accesses;
		std::vector<std::vector<ExecutionBoundaryAccess>> last_accesses;
	};

	namespace details {

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
			} else if (auto texture = std::get_if<TextureRange>(&range)) {
				if (texture->mip_level_count == 0u || texture->array_layer_count == 0u ||
				    texture->base_mip_level >
				        (std::numeric_limits<std::uint32_t>::max)() - texture->mip_level_count ||
				    texture->base_array_layer >
				        (std::numeric_limits<std::uint32_t>::max)() - texture->array_layer_count) {
					throw std::invalid_argument("Command graph contains an invalid texture range");
				}
			}
		}

		bool RangesOverlap(ResourceRange const& lhs, ResourceRange const& rhs) noexcept {
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
			bool mip_overlap = lhs_texture.base_mip_level <
			        rhs_texture->base_mip_level + rhs_texture->mip_level_count &&
			    rhs_texture->base_mip_level <
			        lhs_texture.base_mip_level + lhs_texture.mip_level_count;
			bool layer_overlap = lhs_texture.base_array_layer <
			        rhs_texture->base_array_layer + rhs_texture->array_layer_count &&
			    rhs_texture->base_array_layer <
			        lhs_texture.base_array_layer + lhs_texture.array_layer_count;
			return mip_overlap && layer_overlap;
		}

		bool RangeContains(ResourceRange const& outer, ResourceRange const& inner) noexcept {
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
			return inner_texture && outer_texture.base_mip_level <= inner_texture->base_mip_level &&
			    outer_texture.base_mip_level + outer_texture.mip_level_count >=
			    inner_texture->base_mip_level + inner_texture->mip_level_count &&
			    outer_texture.base_array_layer <= inner_texture->base_array_layer &&
			    outer_texture.base_array_layer + outer_texture.array_layer_count >=
			    inner_texture->base_array_layer + inner_texture->array_layer_count;
		}

		ResourceRange IntersectRanges(ResourceRange const& lhs, ResourceRange const& rhs) noexcept {
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
				auto end = (std::min)(lhs_buffer->offset + lhs_buffer->size,
				                      rhs_buffer->offset + rhs_buffer->size);
				return BufferRange{begin, end - begin};
			}
			auto const& lhs_texture = std::get<TextureRange>(lhs);
			auto rhs_texture = std::get_if<TextureRange>(&rhs);
			if (!rhs_texture) {
				return std::monostate{};
			}
			auto mip = (std::max)(lhs_texture.base_mip_level, rhs_texture->base_mip_level);
			auto mip_end = (std::min)(lhs_texture.base_mip_level + lhs_texture.mip_level_count,
			                          rhs_texture->base_mip_level + rhs_texture->mip_level_count);
			auto layer = (std::max)(lhs_texture.base_array_layer, rhs_texture->base_array_layer);
			auto layer_end =
			    (std::min)(lhs_texture.base_array_layer + lhs_texture.array_layer_count,
				           rhs_texture->base_array_layer + rhs_texture->array_layer_count);
			return TextureRange{
			    .base_mip_level = mip,
			    .mip_level_count = mip_end - mip,
			    .base_array_layer = layer,
			    .array_layer_count = layer_end - layer
			};
		}

		bool MergeRanges(ResourceRange& destination, ResourceRange const& source) noexcept {
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
				if (destination_end < source_buffer->offset ||
				    source_end < destination_buffer->offset) {
					return false;
				}
				auto begin = (std::min)(destination_buffer->offset, source_buffer->offset);
				auto end = (std::max)(destination_end, source_end);
				*destination_buffer = BufferRange{begin, end - begin};
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
				auto begin =
				    (std::min)(destination_texture->base_mip_level, source_texture->base_mip_level);
				auto end = (std::max)(destination_end, source_end);
				destination_texture->base_mip_level = begin;
				destination_texture->mip_level_count = end - begin;
				return true;
			}
			if (same_mips) {
				auto destination_end =
				    destination_texture->base_array_layer + destination_texture->array_layer_count;
				auto source_end =
				    source_texture->base_array_layer + source_texture->array_layer_count;
				if (destination_end < source_texture->base_array_layer ||
				    source_end < destination_texture->base_array_layer) {
					return false;
				}
				auto begin = (std::min)(destination_texture->base_array_layer,
				                        source_texture->base_array_layer);
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

		/// Identity of a barrier transition. Two barriers can merge their ranges only
		/// when every one of these fields matches; the ranges themselves are excluded
		/// because merging is precisely what reconciles them.
		struct BarrierTransition {
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

			bool operator==(BarrierTransition const&) const noexcept = default;
		};

		struct BarrierTransitionHash {
			std::size_t operator()(BarrierTransition const& value) const noexcept {
				std::size_t seed = value.resource;
				auto mix = [&seed](std::size_t hash) noexcept {
					seed ^= hash + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
				};
				mix(value.source_node);
				mix(value.destination_node);
				mix(value.source_batch);
				mix(value.destination_batch);
				mix(static_cast<std::size_t>(value.source_queue));
				mix(static_cast<std::size_t>(value.destination_queue));
				mix(static_cast<std::size_t>(value.source_mode));
				mix(static_cast<std::size_t>(value.destination_mode));
				mix(static_cast<std::size_t>(value.source_usage));
				mix(static_cast<std::size_t>(value.destination_usage));
				return seed;
			}
		};

		BarrierTransition TransitionOf(ExecutionBarrier const& barrier) noexcept {
			return BarrierTransition{
			    .resource = barrier.resource,
			    .source_node = barrier.source_node,
			    .destination_node = barrier.destination_node,
			    .source_batch = barrier.source_batch,
			    .destination_batch = barrier.destination_batch,
			    .source_queue = barrier.source_queue,
			    .destination_queue = barrier.destination_queue,
			    .source_mode = barrier.source_mode,
			    .destination_mode = barrier.destination_mode,
			    .source_usage = barrier.source_usage,
			    .destination_usage = barrier.destination_usage
			};
		}

		/**
		 * @brief Insertion-ordered barriers indexed by their transition.
		 *
		 * Replaces a scan of every accumulated barrier with a lookup of the entries that
		 * share the new barrier's transition. Candidates are still tried in insertion
		 * order, so range merging behaves exactly like the scan it replaces. Several
		 * entries can share one transition when their ranges never merge -- disjoint
		 * buffer ranges, or texture ranges sharing neither mip nor layer extent -- so a
		 * transition maps to every candidate rather than to a single one.
		 */
		class BarrierSet {
		private:
			std::vector<ExecutionBarrier> m_barriers;
			std::unordered_map<BarrierTransition, std::vector<std::size_t>, BarrierTransitionHash>
			    m_candidates;

		public:
			void Add(ExecutionBarrier const& barrier) {
				auto& candidates = m_candidates[TransitionOf(barrier)];
				for (auto candidate : candidates) {
					auto& existing = m_barriers[candidate];
					if (MergeRanges(existing.source_range, barrier.source_range)) {
						existing.destination_range = existing.source_range;
						return;
					}
				}
				candidates.emplace_back(m_barriers.size());
				m_barriers.emplace_back(barrier);
			}

			std::vector<ExecutionBarrier> const& Items() const noexcept {
				return m_barriers;
			}

			/// Hands the barriers over in insertion order; the set is empty afterwards.
			std::vector<ExecutionBarrier> Release() noexcept {
				return std::move(m_barriers);
			}
		};

		bool RequiresBarrier(
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
				if (access.resource == resource && (!read || CanRead(access.mode)) &&
				    (!write || CanWrite(access.mode)) && HasUsage(access.usage, usage)) {
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
				ValidateIndex(
				    source,
				    graph.bindings.resource_count,
				    "Copy command contains an invalid source resource"
				);
				ValidateIndex(
				    destination,
				    graph.bindings.resource_count,
				    "Copy command contains an invalid destination resource"
				);
				ValidateAccess(
				    node,
				    source,
				    true,
				    false,
				    ResourceUsage::CopySource,
				    "Copy source is missing its read copy-source access"
				);
				ValidateAccess(
				    node,
				    destination,
				    false,
				    true,
				    ResourceUsage::CopyDestination,
				    "Copy destination is missing its write copy-destination access"
				);
			}

			void operator()(BeginRendering const& command) const {
				RequireQueue(QueueType::Graphics, "BeginRendering requires a graphics node");
				if (command.area.x < 0 || command.area.y < 0 || command.area.width == 0u ||
				    command.area.height == 0u) {
					throw std::invalid_argument(
					    "BeginRendering contains an invalid rendering area"
					);
				}
				for (auto const& color : command.colors) {
					ValidateIndex(
					    color.resource,
					    graph.bindings.resource_count,
					    "Color attachment contains an invalid resource"
					);
					ValidateIndex(
					    color.view,
					    graph.bindings.view_count,
					    "Color attachment contains an invalid view"
					);
					ValidateAccess(
					    node,
					    color.resource,
					    color.load == LoadOperation::Load,
					    true,
					    ResourceUsage::ColorAttachment,
					    "Color attachment is missing its declared attachment access"
					);
					if (static_cast<bool>(color.resolve_resource) !=
					    static_cast<bool>(color.resolve_view)) {
						throw std::invalid_argument(
						    "Resolve attachment requires both a resource and a view"
						);
					}
					if (color.resolve_resource) {
						ValidateIndex(
						    *color.resolve_resource,
						    graph.bindings.resource_count,
						    "Resolve attachment contains an invalid resource"
						);
						ValidateIndex(
						    *color.resolve_view,
						    graph.bindings.view_count,
						    "Resolve attachment contains an invalid view"
						);
						ValidateAccess(
						    node,
						    color.resource,
						    true,
						    false,
						    ResourceUsage::ResolveSource,
						    "Resolve source is missing its read resolve-source access"
						);
						ValidateAccess(
						    node,
						    *color.resolve_resource,
						    false,
						    true,
						    ResourceUsage::ResolveDestination,
						    "Resolve destination is missing its write resolve-destination access"
						);
					}
				}
				if (command.depth_stencil) {
					ValidateIndex(
					    command.depth_stencil->resource,
					    graph.bindings.resource_count,
					    "Depth/stencil attachment contains an invalid resource"
					);
					ValidateIndex(
					    command.depth_stencil->view,
					    graph.bindings.view_count,
					    "Depth/stencil attachment contains an invalid view"
					);
					bool read = command.depth_stencil->depth_load == LoadOperation::Load ||
					    command.depth_stencil->stencil_load == LoadOperation::Load;
					bool write = command.depth_stencil->depth_load == LoadOperation::Clear ||
					    command.depth_stencil->stencil_load == LoadOperation::Clear ||
					    command.depth_stencil->depth_store == StoreOperation::Store ||
					    command.depth_stencil->stencil_store == StoreOperation::Store;
					ValidateAccess(
					    node,
					    command.depth_stencil->resource,
					    read,
					    write,
					    ResourceUsage::DepthStencilAttachment,
					    "Depth/stencil attachment is missing its declared attachment access"
					);
				}
			}

			void operator()(EndRendering const&) const {
				RequireQueue(QueueType::Graphics, "EndRendering requires a graphics node");
			}

			void operator()(BindPipeline const& command) const {
				if (node.queue != QueueType::Graphics && node.queue != QueueType::Compute) {
					throw std::invalid_argument("BindPipeline requires a graphics or compute node");
				}
				ValidateIndex(
				    command.pipeline,
				    graph.bindings.pipeline_count,
				    "BindPipeline contains an invalid pipeline"
				);
			}

			void operator()(BindResourceGroup const& command) const {
				if (node.queue != QueueType::Graphics && node.queue != QueueType::Compute) {
					throw std::invalid_argument(
					    "BindResourceGroup requires a graphics or compute node"
					);
				}
				ValidateIndex(
				    command.group,
				    graph.bindings.resource_group_count,
				    "BindResourceGroup contains an invalid resource group"
				);
			}

			void operator()(SetPipelineConstants const& command) const {
				if (node.queue != QueueType::Graphics && node.queue != QueueType::Compute) {
					throw std::invalid_argument(
					    "SetPipelineConstants requires a graphics or compute node"
					);
				}
				if (command.data.empty() || command.offset % sizeof(std::uint32_t) != 0u ||
				    command.data.size() % sizeof(std::uint32_t) != 0u) {
					throw std::invalid_argument(
					    "SetPipelineConstants requires non-empty, four-byte-aligned data"
					);
				}
			}

			void operator()(BindVertexBuffer const& command) const {
				RequireQueue(QueueType::Graphics, "BindVertexBuffer requires a graphics node");
				ValidateIndex(
				    command.resource,
				    graph.bindings.resource_count,
				    "BindVertexBuffer contains an invalid resource"
				);
				if (command.stride == 0u) {
					throw std::invalid_argument("BindVertexBuffer stride must be non-zero");
				}
				ValidateAccess(
				    node,
				    command.resource,
				    true,
				    false,
				    ResourceUsage::VertexBuffer,
				    "Vertex buffer is missing its read vertex-buffer access"
				);
			}

			void operator()(BindIndexBuffer const& command) const {
				RequireQueue(QueueType::Graphics, "BindIndexBuffer requires a graphics node");
				ValidateIndex(
				    command.resource,
				    graph.bindings.resource_count,
				    "BindIndexBuffer contains an invalid resource"
				);
				ValidateAccess(
				    node,
				    command.resource,
				    true,
				    false,
				    ResourceUsage::IndexBuffer,
				    "Index buffer is missing its read index-buffer access"
				);
			}

			void operator()(Viewport const& command) const {
				RequireQueue(QueueType::Graphics, "Viewport requires a graphics node");
				if (command.width < 0.0f || command.height < 0.0f || command.minimum_depth < 0.0f ||
				    command.maximum_depth > 1.0f || command.minimum_depth > command.maximum_depth) {
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
				ValidateTextureLayout(
				    command.source_layout,
				    "CopyBufferToTexture contains an invalid source layout"
				);
				ValidateTextureRegion(
				    command.destination_region,
				    "CopyBufferToTexture contains an invalid destination region"
				);
			}

			void operator()(CopyTextureToBuffer const& command) const {
				ValidateCopy(command.source, command.destination);
				ValidateTextureRegion(
				    command.source_region,
				    "CopyTextureToBuffer contains an invalid source region"
				);
				ValidateTextureLayout(
				    command.destination_layout,
				    "CopyTextureToBuffer contains an invalid destination layout"
				);
			}

			void operator()(CopyTexture const& command) const {
				ValidateCopy(command.source, command.destination);
				ValidateTextureRegion(
				    command.source_region,
				    "CopyTexture contains an invalid source region"
				);
				ValidateTextureRegion(
				    command.destination_region,
				    "CopyTexture contains an invalid destination region"
				);
				if (command.source_region.width != command.destination_region.width ||
				    command.source_region.height != command.destination_region.height ||
				    command.source_region.depth != command.destination_region.depth ||
				    command.source_region.array_layer_count !=
				        command.destination_region.array_layer_count) {
					throw std::invalid_argument(
					    "CopyTexture source and destination extents differ"
					);
				}
			}

			void operator()(WriteBuffer const& command) const {
				RequireQueue(QueueType::Transfer, "WriteBuffer requires a transfer node");
				ValidateIndex(
				    command.resource,
				    graph.bindings.resource_count,
				    "WriteBuffer contains an invalid resource"
				);
				if (command.data.empty()) {
					throw std::invalid_argument("WriteBuffer data must not be empty");
				}
				if (command.offset >
				    (std::numeric_limits<std::size_t>::max)() - command.data.size()) {
					throw std::invalid_argument("WriteBuffer range overflows");
				}
				// WriteBuffer is a host-side write (D3D12 Map / vmaMapMemory / MapAsync):
				// it does not change the GPU-side resource state, so it declares no
				// access of its own. The target only needs a read/copy-source access if
				// it is subsequently consumed by a CopyBuffer/CopyBufferToTexture.
			}

			void operator()(Present const& command) const {
				RequireQueue(QueueType::Present, "Present requires a presentation node");
				ValidateIndex(
				    command.source,
				    graph.bindings.resource_count,
				    "Present contains an invalid source resource"
				);
				if (command.buffer_count == 0u) {
					throw std::invalid_argument("Present buffer count must not be zero");
				}
				ValidateAccess(
				    node,
				    command.source,
				    true,
				    false,
				    ResourceUsage::PresentationSource,
				    "Presentation source is missing its read presentation-source access"
				);
			}
		};

		ExecutionPlan CompileExecutionPlan(ExecutionGraph&& graph) {
			// The builder hands its graph over for the last time here, so the recorded
			// commands move into the plan's batches instead of being deep-copied.
			auto nodes = std::move(graph.nodes);
			std::vector<std::size_t> indegrees(nodes.size(), 0u);
			std::vector<std::vector<std::size_t>> dependents(nodes.size());
			for (std::size_t index = 0u; index < nodes.size(); ++index) {
				auto const& node = nodes[index];
				if (node.id != index) {
					throw std::invalid_argument(
					    "Command graph node IDs must match their storage indices"
					);
				}
				for (
				    std::size_t dependency_index = 0u; dependency_index < node.dependencies.size();
				    ++dependency_index
				) {
					auto dependency = node.dependencies[dependency_index];
					if (dependency >= nodes.size() || dependency == node.id) {
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
					ValidateIndex(
					    access.resource,
					    graph.bindings.resource_count,
					    "Command graph access contains an invalid resource"
					);
					if (access.usage == ResourceUsage::None) {
						throw std::invalid_argument("Command graph access usage must not be empty");
					}
					ValidateRange(access.range);
				}

				bool rendering = false;
				for (auto const& command : node.commands) {
					if (std::holds_alternative<BeginRendering>(command)) {
						if (rendering) {
							throw std::invalid_argument(
							    "Command graph contains nested rendering scopes"
							);
						}
						rendering = true;
					} else if (std::holds_alternative<EndRendering>(command)) {
						if (!rendering) {
							throw std::invalid_argument(
							    "EndRendering has no matching BeginRendering"
							);
						}
						rendering = false;
					} else if (
					    (std::holds_alternative<Draw>(command) ||
						 std::holds_alternative<DrawIndexed>(command)) &&
					    !rendering
					) {
						throw std::invalid_argument(
						    "Draw commands require an active rendering scope"
						);
					} else if (
					    (std::holds_alternative<Dispatch>(command) ||
						 std::holds_alternative<CopyBuffer>(command) ||
						 std::holds_alternative<CopyBufferToTexture>(command) ||
						 std::holds_alternative<CopyTextureToBuffer>(command) ||
						 std::holds_alternative<CopyTexture>(command) ||
						 std::holds_alternative<WriteBuffer>(command) ||
						 std::holds_alternative<Present>(command)) &&
					    rendering
					) {
						throw std::invalid_argument(
						    "Dispatch, copy, and presentation commands cannot execute in a "
							"rendering scope"
						);
					}
					std::visit(CommandValidator{graph, node}, command);
				}
				if (rendering) {
					throw std::invalid_argument(
					    "Rendering scope must end in the node where it begins"
					);
				}
			}

			// Ready nodes are indexed by id as well as by queue, so the selection rule --
			// prefer the previous node's queue, otherwise the lowest id -- stays exact
			// without scanning the whole ready set on every step.
			std::set<std::size_t> ready_by_id;
			std::map<QueueType, std::set<std::size_t>> ready_by_queue;
			auto mark_ready = [&](std::size_t id) {
				ready_by_id.insert(id);
				ready_by_queue[nodes[id].queue].insert(id);
			};
			for (std::size_t index = 0u; index < indegrees.size(); ++index) {
				if (indegrees[index] == 0u) {
					mark_ready(index);
				}
			}
			ExecutionPlan plan;
			plan.bindings = graph.bindings;
			plan.topological_order.reserve(nodes.size());
			std::optional<QueueType> preferred_queue;
			while (!ready_by_id.empty()) {
				std::size_t node = *ready_by_id.begin();
				if (preferred_queue) {
					auto const preferred = ready_by_queue.find(*preferred_queue);
					if (preferred != ready_by_queue.end() && !preferred->second.empty()) {
						node = *preferred->second.begin();
					}
				}
				auto& queue_set = ready_by_queue[nodes[node].queue];
				queue_set.erase(node);
				if (queue_set.empty()) {
					ready_by_queue.erase(nodes[node].queue);
				}
				ready_by_id.erase(node);
				plan.topological_order.emplace_back(node);
				preferred_queue = nodes[node].queue;
				for (auto dependent : dependents[node]) {
					if (--indegrees[dependent] == 0u) {
						mark_ready(dependent);
					}
				}
			}
			if (plan.topological_order.size() != nodes.size()) {
				throw std::invalid_argument("Command graph contains a dependency cycle");
			}

			struct PreviousAccess {
				std::size_t node;
				ResourceAccess access;
			};
			std::vector<std::vector<PreviousAccess>> previous_accesses(
			    graph.bindings.resource_count
			);
			BarrierSet barriers;
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
						barriers.Add(
						    ExecutionBarrier{
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
						    }
						);
					}
					for (
					    auto previous_access = previous.begin(); previous_access != previous.end();
					) {
						bool replace = RangesOverlap(previous_access->access.range, access.range) &&
						    RequiresBarrier(
						                   previous_access->access,
						                   nodes[previous_access->node].queue,
						                   access,
						                   nodes[node_id].queue
						    );
						if (replace) {
							previous_access = previous.erase(previous_access);
						} else {
							++previous_access;
						}
					}
					previous.emplace_back(PreviousAccess{node_id, access});
				}
			}

			plan.node_batches.resize(nodes.size());
			for (auto node_id : plan.topological_order) {
				auto const& node = nodes[node_id];
				if (plan.batches.empty() || plan.batches.back().queue != node.queue) {
					auto batch_id = plan.batches.size();
					plan.batches.push_back({.id = batch_id, .queue = node.queue});
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
			// Batch assignment changes the transition identity these barriers are indexed
			// by, so it is applied to a copy instead of to the accumulated set.
			std::vector<ExecutionBarrier> assigned_barriers;
			assigned_barriers.reserve(barriers.Items().size());
			for (auto const& barrier : barriers.Items()) {
				auto value = barrier;
				value.source_batch = plan.node_batches[value.source_node];
				value.destination_batch = plan.node_batches[value.destination_node];
				assigned_barriers.emplace_back(value);
			}
			std::vector<BarrierSet> batch_barriers(plan.batches.size());
			std::vector<BarrierSet> batch_release_barriers(plan.batches.size());
			for (auto const& barrier : assigned_barriers) {
				batch_barriers[barrier.destination_batch].Add(barrier);
				if (barrier.CrossQueue()) {
					batch_release_barriers[barrier.source_batch].Add(barrier);
				}
			}
			for (std::size_t index = 0u; index < plan.batches.size(); ++index) {
				plan.batches[index].barriers = batch_barriers[index].Release();
				plan.batches[index].release_barriers = batch_release_barriers[index].Release();
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
			for (
			    auto node = plan.topological_order.rbegin(); node != plan.topological_order.rend();
			    ++node
			) {
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

		/**
		 * @brief Move-only receiver the completion driver invokes with its stop token.
		 *
		 * Hand-written rather than std::function or std::move_only_function: a callable that fits
		 * in the buffer below is stored inline, so the receivers built here queue without touching
		 * the heap, and a larger one costs a single allocation instead of an allocation plus a
		 * second wrapper. One dispatch table per callable type keeps the call to a single indirect
		 * call, with no virtual dispatch and no copying: a queued task is moved into the queue.
		 */
		class CompletionTask {
		private:
			/// How one stored callable is reached, destroyed and relocated.
			struct Dispatch {
				void (*invoke)(void* storage, std::stop_token stop);
				void (*destroy)(void* storage) noexcept;
				void (*move)(void* from, void* to) noexcept;
			};

			/// Three pointers, which is what the receivers here need: one unique_ptr and one
			/// CompletionToken.
			static constexpr std::size_t kInlineSize = 3 * sizeof(void*);
			static constexpr std::size_t kInlineAlignment = alignof(void*);

			/// Dispatch for the callable type @p Function, choosing where it is stored.
			template <class Function> struct Table {
			public:
				/// Inline storage demands a non-throwing move, because moving a CompletionTask is
				/// noexcept; a callable that cannot promise that is stored on the heap, where
				/// moving only transfers the pointer.
				static constexpr bool inline_stored = sizeof(Function) <= kInlineSize &&
				    alignof(Function) <= kInlineAlignment &&
				    std::is_nothrow_move_constructible_v<Function>;

			private:
				static std::byte* Bytes(void* storage) noexcept {
					return static_cast<std::byte*>(storage);
				}

				/// The callable held in @p storage; only meaningful while a table is installed.
				static Function* Target(void* storage) noexcept {
					if constexpr (inline_stored) {
						return std::launder(reinterpret_cast<Function*>(Bytes(storage)));
					} else {
						return *reinterpret_cast<Function**>(Bytes(storage));
					}
				}

				static void Invoke(void* storage, std::stop_token stop) {
					(*Target(storage))(stop);
				}

				static void Destroy(void* storage) noexcept {
					if constexpr (inline_stored) {
						std::destroy_at(Target(storage));
					} else {
						delete Target(storage);
					}
				}

				/// Moves the callable into the unset buffer @p to and empties @p from; the owner
				/// hands over ownership by moving its table pointer across in the same step.
				static void Move(void* from, void* to) noexcept {
					if constexpr (inline_stored) {
						std::construct_at(
						    reinterpret_cast<Function*>(Bytes(to)),
						    std::move(*Target(from))
						);
					} else {
						*reinterpret_cast<Function**>(Bytes(to)) =
						    std::exchange(*reinterpret_cast<Function**>(Bytes(from)), nullptr);
					}
				}

			public:
				static constexpr Dispatch value{&Invoke, &Destroy, &Move};
			};

			/// Null for an empty task, which is the only state whose call is undefined.
			const Dispatch* m_table = nullptr;
			alignas(kInlineAlignment) std::byte m_storage[kInlineSize];

			void Reset() noexcept {
				if (m_table) {
					m_table->destroy(m_storage);
					m_table = nullptr;
				}
			}

			template <class Function> void Emplace(Function&& function) {
				using Target = std::remove_cvref_t<Function>;
				if constexpr (Table<Target>::inline_stored) {
					std::construct_at(
					    reinterpret_cast<Target*>(m_storage),
					    std::forward<Function>(function)
					);
				} else {
					// One allocation for the whole callable; the buffer holds only its address.
					std::construct_at(
					    reinterpret_cast<Target**>(m_storage),
					    new Target(std::forward<Function>(function))
					);
				}
				m_table = &Table<Target>::value;
			}

		public:
			CompletionTask() noexcept = default;

			template <class Function>
			    requires(!std::same_as<std::remove_cvref_t<Function>, CompletionTask>)
			explicit CompletionTask(Function&& function) {
				Emplace(std::forward<Function>(function));
			}

			CompletionTask(CompletionTask&& other) noexcept :
			    m_table(std::exchange(other.m_table, nullptr)) {
				if (m_table) {
					m_table->move(other.m_storage, m_storage);
				}
			}

			CompletionTask& operator=(CompletionTask&& other) noexcept {
				if (this != &other) {
					Reset();
					m_table = std::exchange(other.m_table, nullptr);
					if (m_table) {
						m_table->move(other.m_storage, m_storage);
					}
				}
				return *this;
			}

			CompletionTask(CompletionTask const&) = delete;
			CompletionTask& operator=(CompletionTask const&) = delete;

			~CompletionTask() noexcept {
				Reset();
			}

			/// Invokes the stored callable; calling an empty task is undefined.
			void operator()(std::stop_token stop) {
				m_table->invoke(m_storage, stop);
			}

			/// Whether @p Function is stored inline rather than in one heap allocation.
			template <class Function>
			static constexpr bool FitsInline = Table<Function>::inline_stored;
		};

		/// Wraps @p function into a CompletionTask, storing it inline when it fits.
		template <class Function> CompletionTask MakeCompletionTask(Function&& function) {
			return CompletionTask(std::forward<Function>(function));
		}

		/**
		 * @brief Hands a finished operation's completion task to the completion driver.
		 *
		 * Defined in an implementation unit, where the driver keeps its state on the stack of the
		 * thread that owns it rather than in a heap object exported from this module.
		 */
		void EnqueueCompletionTask(CompletionTask&& task);

		/// Drains and stops the completion driver; see ShutdownCompletionPool().
		void ShutdownCompletionDriver();

	} // namespace details

#if defined(_WIN32)
	using PlatformHandle = HWND;
#elif defined(__ANDROID__)
	using PlatformHandle = ANativeWindow*;
#elif defined(__linux__)
	struct X11PlatformHandle {
		Display* display;
		Window window;
	};

	struct WaylandPlatformHandle {
		wl_display* display;
		wl_surface* surface;
	};

	using PlatformHandle = std::variant<X11PlatformHandle, WaylandPlatformHandle>;
#elif defined(__APPLE__)
	using PlatformHandle = CA::MetalLayer*;
#endif // defined(_WIN32)

	/**
	 * @brief Move-only backend completion state for submitted GPU work.
	 *
	 * Poll() is non-blocking, Wait() blocks. Once complete, Error() contains a captured
	 * failure or IsStopped() reports cancellation; a successful token has neither.
	 */
	class CompletionToken {
	public:
		using UniqueHandle = std::unique_ptr<
		    struct CompletionTokenImplementation,
		    void (*)(struct CompletionTokenImplementation*)>;

	private:
		UniqueHandle m_impl;

	public:
		CompletionToken() noexcept : m_impl(nullptr, nullptr) {
		}

		explicit CompletionToken(UniqueHandle&& impl) noexcept : m_impl(std::move(impl)) {
		}

		bool Poll() noexcept;

		/**
		 * @brief Blocks until the work reaches a terminal state or a stop is requested.
		 *
		 * Returns true when Poll() reports a terminal state, so Error()/IsStopped() are
		 * meaningful and the bound objects are safe to use. Returns false only when
		 * @p stop_token was observed requested while the work was still incomplete; the
		 * GPU work itself is not cancellable once submitted.
		 *
		 * The wait itself is event-driven where the backend offers a primitive for it: a
		 * normal completion is observed as soon as the GPU signals it, and a stop request is
		 * honoured within the backend's own bounded blocking granularity.
		 */
		bool Wait(std::stop_token stop_token) noexcept;

		std::exception_ptr Error() noexcept;

		bool IsStopped() noexcept;
	};

	struct CommandSchedulerContext;
	class CommandScheduler;
	template <class Receiver> class CommandGraphBindings;

	template <class Owner> class PassKey {
		friend Owner;

		PassKey() noexcept {
		}

		PassKey(PassKey const&) noexcept {
		}
	};

	/**
	 * @brief Owns every object returned after a command graph reaches a terminal state.
	 *
	 * Each Take function consumes its indexed slot exactly once. Indices are the
	 * values returned by the matching CommandGraphBuilder Register function.
	 */
	class CommandGraphResources {
	private:
		std::vector<Resource> m_resources;
		std::vector<View> m_views;
		std::vector<Sampler> m_samplers;
		std::vector<Pipeline> m_pipelines;
		std::vector<PipelineResourceGroup> m_resource_groups;

		template <class Receiver> friend class CommandGraphBindings;

		template <class Value> static Value Take(std::vector<Value>& values, std::size_t index) {
			if (index >= values.size()) {
				throw std::out_of_range("Command graph result index is out of range");
			}
			auto& value = values[index];
			if (!value) {
				throw std::logic_error("Command graph result object is unavailable");
			}
			return std::move(value);
		}

		CommandGraphResources(
		    std::vector<Resource>&& resources,
		    std::vector<View>&& views,
		    std::vector<Sampler>&& samplers,
		    std::vector<Pipeline>&& pipelines,
		    std::vector<PipelineResourceGroup>&& resource_groups
		) noexcept :
		    m_resources(std::move(resources)), m_views(std::move(views)),
		    m_samplers(std::move(samplers)), m_pipelines(std::move(pipelines)),
		    m_resource_groups(std::move(resource_groups)) {
		}

	public:
		Resource TakeResource(std::size_t index) {
			return Take(m_resources, index);
		}

		View TakeView(std::size_t index) {
			return Take(m_views, index);
		}

		Sampler TakeSampler(std::size_t index) {
			return Take(m_samplers, index);
		}

		Pipeline TakePipeline(std::size_t index) {
			return Take(m_pipelines, index);
		}

		PipelineResourceGroup TakeResourceGroup(std::size_t index) {
			return Take(m_resource_groups, index);
		}
	};

#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
	template <class Receiver>
	concept CommandGraphReceiver = std::move_constructible<Receiver> &&
	    requires(Receiver& receiver,
		         Receiver&& completed_receiver,
		         CommandGraphResources&& resources,
		         std::exception_ptr error) {
		    { receiver.RecoverBindings(std::move(resources)) } noexcept;
		    std::execution::get_env(receiver);
		    {
			    std::execution::set_value(std::move(completed_receiver), std::move(resources))
		    } noexcept;
		    { std::execution::set_error(std::move(completed_receiver), error) } noexcept;
		    { std::execution::set_stopped(std::move(completed_receiver)) } noexcept;
	    };
#else
	template <class Receiver>
	concept CommandGraphReceiver = std::move_constructible<Receiver> &&
	    requires(Receiver& receiver,
		         Receiver&& completed_receiver,
		         CommandGraphResources&& resources,
		         std::exception_ptr error) {
		    receiver.get_env();
		    { receiver.RecoverBindings(std::move(resources)) } noexcept;
		    { std::move(completed_receiver).set_value(std::move(resources)) } noexcept;
		    { std::move(completed_receiver).set_error(error) } noexcept;
		    { std::move(completed_receiver).set_stopped() } noexcept;
	    };
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

	/**
	 * @brief Single-use builder for a dependency-ordered command graph.
	 *
	 * Register typed binding slots, create nodes, declare accesses, record commands,
	 * then move the builder into connect(). Actual GPU objects are moved into the
	 * resulting CommandGraphBindings.
	 */
	class CommandGraphBuilder {
	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using sender_concept = std::execution::sender_t;
		using completion_signatures = std::execution::completion_signatures<
		    std::execution::set_value_t(CommandGraphResources),
		    std::execution::set_error_t(std::exception_ptr),
		    std::execution::set_stopped_t()>;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
	private:
		friend class CommandScheduler;

		std::shared_ptr<CommandSchedulerContext> m_context;
		ExecutionGraph m_graph;

		explicit CommandGraphBuilder(
		    std::shared_ptr<CommandSchedulerContext> const& context
		) noexcept : m_context(context), m_graph() {
		}

		ExecutionNode& GetNode(std::size_t id) {
			if (id >= m_graph.nodes.size() || m_graph.nodes[id].id != id) {
				throw std::out_of_range("Command graph node does not exist");
			}
			return m_graph.nodes[id];
		}

	public:
		/** @brief Lightweight handle used to edit one node owned by its builder. */
		class Node {
			friend class CommandGraphBuilder;

			CommandGraphBuilder* m_builder;
			std::size_t m_index;

			Node(CommandGraphBuilder* builder, std::size_t index) noexcept :
			    m_builder(builder), m_index(index) {
			}

		public:
			/// Adds a directed dependency; both nodes must belong to this builder.
			Node& DependsOn(Node const& dependency) {
				if (m_builder != dependency.m_builder) {
					throw std::invalid_argument(
					    "Command graph dependency belongs to another builder"
					);
				}
				m_builder->GetNode(m_index).dependencies.emplace_back(dependency.m_index);
				return *this;
			}

			/// Declares a resource range accessed by this node.
			Node& Access(ResourceAccess const& access) {
				m_builder->GetNode(m_index).accesses.emplace_back(access);
				return *this;
			}

			template <class Command>
			    requires std::constructible_from<CommandRecord, Command>
			/// Appends one command in node-local recording order.
			Node& Record(Command&& command) {
				m_builder->GetNode(m_index).commands.emplace_back(std::forward<Command>(command));
				return *this;
			}
		};

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
			m_graph.nodes.push_back({.id = id, .queue = queue});
			Node node{this, id};
			if (dependency) {
				node.DependsOn(*dependency);
			}
			return node;
		}

		template <class Receiver>
		    requires CommandGraphReceiver<std::remove_cvref_t<Receiver>>
		CommandGraphBindings<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) &&;
	};

	/**
	 * @brief Copyable entry point for building and submitting command graphs.
	 *
	 * Copies share native scheduling state. Independent graphs may execute
	 * concurrently; synchronization is restricted to native objects that require it.
	 */
	class CommandScheduler {
	private:
		std::shared_ptr<struct CommandSchedulerContext> m_impl;

		CompletionToken Execute(
		    ExecutionPlan const&,
		    std::span<PlatformHandle const>,
		    std::span<Resource const>,
		    std::span<View const>,
		    std::span<Sampler const>,
		    std::span<Pipeline const>,
		    std::span<PipelineResourceGroup const>,
		    StopTokenView
		);

	public:
		template <class Receiver>
		CompletionToken Execute(
		    PassKey<CommandGraphBindings<Receiver>>,
		    ExecutionPlan const& plan,
		    std::span<PlatformHandle const> presentation_targets,
		    std::span<Resource const> resources,
		    std::span<View const> views,
		    std::span<Sampler const> samplers,
		    std::span<Pipeline const> pipelines,
		    std::span<PipelineResourceGroup const> resource_groups,
		    StopTokenView stop_token
		) {
			return Execute(
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

		CommandScheduler() noexcept = default;

		explicit CommandScheduler(std::shared_ptr<CommandSchedulerContext> const& impl) noexcept :
		    m_impl(impl) {
		}

		/// Starts a fresh graph without reserving queues or creating GPU sync objects.
		CommandGraphBuilder schedule() const noexcept {
			return CommandGraphBuilder{m_impl};
		}
	};

	/**
	 * @brief Move-only operation state containing a planned graph and its GPU objects.
	 *
	 * Bind every registered slot exactly once, set presentation targets, then call
	 * start(). On success objects are delivered to set_value(); before an error or
	 * cancellation they are first returned through RecoverBindings(). Completion
	 * may run on an internal worker thread.
	 */
	template <class Receiver> class CommandGraphBindings {
	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using operation_state_concept = std::execution::operation_state_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

	private:
		struct OperationStateContext {
			CommandScheduler scheduler;
			ExecutionPlan plan;
			std::vector<Resource> resources;
			std::vector<View> views;
			std::vector<Sampler> samplers;
			std::vector<Pipeline> pipelines;
			std::vector<PipelineResourceGroup> resource_groups;
			std::vector<PlatformHandle> presentation_targets;
			Receiver receiver;

			OperationStateContext(
			    CommandScheduler const& scheduler_,
			    ExecutionPlan&& plan_,
			    Receiver&& receiver_
			) :
			    scheduler(scheduler_), plan(std::move(plan_)),
			    resources(plan.bindings.resource_count), views(plan.bindings.view_count),
			    samplers(plan.bindings.sampler_count), pipelines(plan.bindings.pipeline_count),
			    resource_groups(plan.bindings.resource_group_count),
			    receiver(std::move(receiver_)) {
			}
		};

		std::unique_ptr<OperationStateContext> m_context;

		template <class Value>
		static void BindAt(std::vector<Value>& bindings, std::size_t index, Value&& value) {
			if (index >= bindings.size()) {
				throw std::out_of_range("Command graph binding index is out of range");
			}
			if (bindings[index]) {
				throw std::logic_error("Command graph binding is already occupied");
			}
			bindings[index] = std::move(value);
		}

		template <class Value>
		static void ValidateBindings(std::vector<Value> const& bindings, char const* message) {
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

		static CommandGraphResources ReleaseBindings(OperationStateContext& context) noexcept {
			return CommandGraphResources(
			    std::move(context.resources),
			    std::move(context.views),
			    std::move(context.samplers),
			    std::move(context.pipelines),
			    std::move(context.resource_groups)
			);
		}

		static void SetValue(std::unique_ptr<OperationStateContext>&& context) noexcept {
			auto resources = ReleaseBindings(*context);
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_value(std::move(context->receiver), std::move(resources));
#else
			std::move(context->receiver).set_value(std::move(resources));
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

		static void SetError(
		    std::unique_ptr<OperationStateContext>&& context,
		    std::exception_ptr&& error
		) noexcept {
			context->receiver.RecoverBindings(ReleaseBindings(*context));
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_error(std::move(context->receiver), std::move(error));
#else
			std::move(context->receiver).set_error(std::move(error));
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

		static void SetStopped(std::unique_ptr<OperationStateContext>&& context) noexcept {
			context->receiver.RecoverBindings(ReleaseBindings(*context));
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_stopped(std::move(context->receiver));
#else
			std::move(context->receiver).set_stopped();
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

	public:
		CommandGraphBindings(
		    CommandScheduler const& scheduler,
		    ExecutionPlan&& plan,
		    Receiver&& receiver
		) :
		    m_context(
		        std::make_unique<OperationStateContext>(
		            scheduler,
		            std::move(plan),
		            std::move(receiver)
		        )
		    ) {
		}

		CommandGraphBindings(CommandGraphBindings const&) = delete;
		CommandGraphBindings& operator=(CommandGraphBindings const&) = delete;
		CommandGraphBindings(CommandGraphBindings&&) noexcept = default;
		CommandGraphBindings& operator=(CommandGraphBindings&&) noexcept = default;
		~CommandGraphBindings() noexcept = default;

		void BindResource(std::size_t index, Resource&& resource) {
			BindAt(m_context->resources, index, std::move(resource));
		}

		void BindView(std::size_t index, View&& view) {
			BindAt(m_context->views, index, std::move(view));
		}

		void BindSampler(std::size_t index, Sampler&& sampler) {
			BindAt(m_context->samplers, index, std::move(sampler));
		}

		void BindPipeline(std::size_t index, Pipeline&& pipeline) {
			BindAt(m_context->pipelines, index, std::move(pipeline));
		}

		void BindResourceGroup(std::size_t index, PipelineResourceGroup&& resource_group) {
			BindAt(m_context->resource_groups, index, std::move(resource_group));
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
					auto token = m_context->scheduler.Execute(
					    PassKey<CommandGraphBindings>{},
					    m_context->plan,
					    m_context->presentation_targets,
					    m_context->resources,
					    m_context->views,
					    m_context->samplers,
					    m_context->pipelines,
					    m_context->resource_groups,
					    stop_token
					);
					if (token.IsStopped()) {
						SetStopped(std::move(m_context));
						return;
					}
					// Holds one unique_ptr and one CompletionToken, which is exactly the inline
					// size of a CompletionTask, so enqueueing this receiver allocates nothing.
					details::EnqueueCompletionTask(
					    details::MakeCompletionTask(
					        [operation = std::move(m_context),
							 token = std::move(token)](std::stop_token pool_stop) mutable noexcept {
						        // false means the wait observed a stop request. Anything else
						        // is a terminal state whose result is safe to deliver, so a
						        // bounded wait must never report false on its own.
						        if (!token.Wait(pool_stop)) {
							        SetStopped(std::move(operation));
							        return;
						        }
						        if (auto error = token.Error()) {
							        SetError(std::move(operation), std::move(error));
						        } else if (token.IsStopped()) {
							        SetStopped(std::move(operation));
						        } else {
							        SetValue(std::move(operation));
						        }
					        }
					    )
					);
				};
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
				auto env = std::execution::get_env(m_context->receiver);
				if constexpr (requires { std::execution::get_stop_token(env); }) {
					auto stop_token = std::execution::get_stop_token(env);
					Execute(stop_token);
				} else {
					Execute({});
				}
#else
				auto env = m_context->receiver.get_env();
				if constexpr (requires { env.get_stop_token(); }) {
					auto stop_token = env.get_stop_token();
					Execute(stop_token);
				} else {
					Execute({});
				}
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			} catch (...) {
				SetError(std::move(m_context), std::current_exception());
			}
		}
	};

	/**
	 * @brief Waits for every in-flight operation to report, and stops the internal driver thread.
	 *
	 * Every receiver with work still in flight observes exactly one terminal signal, and the
	 * objects it recovers through RecoverBindings are returned. The work was already submitted,
	 * so these are real results rather than cancellations: this returns once they have been
	 * delivered. Call it while the devices, resources, and log sinks that receivers touch are
	 * still alive; draining from a later static destructor is not supported because those objects
	 * may already be gone.
	 *
	 * Call it when no other thread is submitting graphs, so it does not race with a concurrent
	 * start(). After it returns, further graph operations deliver their completion synchronously
	 * on the submitting thread. A process that never calls it abandons whatever is still queued
	 * at exit, reporting set_stopped, rather than running receiver code during static destruction.
	 */
	inline void ShutdownCompletionPool() {
		details::ShutdownCompletionDriver();
	}

	template <class Receiver>
	    requires CommandGraphReceiver<std::remove_cvref_t<Receiver>>
	CommandGraphBindings<std::remove_cvref_t<Receiver>> CommandGraphBuilder::connect(
	    Receiver&& receiver
	) && {
		return CommandGraphBindings<std::remove_cvref_t<Receiver>>(
		    CommandScheduler{m_context},
		    details::CompileExecutionPlan(std::move(m_graph)),
		    std::forward<Receiver>(receiver)
		);
	}
} // namespace fyuu_rhi::execution
