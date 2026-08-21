module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>
#include <deque>
#include <vector>

#include <algorithm>
#include <functional>
#include <limits>

#include <cstdint>
#include <type_traits>

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

	enum class QueueType : std::uint8_t {
		Graphics,
		Compute,
		Transfer,
		Present
	};

	enum class AccessMode : std::uint8_t {
		Read,
		Write,
		ReadWrite
	};

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

	class StopTokenView {
	private:
		void const* m_token = nullptr;
		bool (*m_stop_requested)(void const*) noexcept = nullptr;

		template <class StopToken>
		static bool StopRequested(void const* token) noexcept {
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

	struct ResourceAccess {
		std::size_t resource;
		AccessMode mode = AccessMode::Read;
		ResourceUsage usage = ResourceUsage::None;
		ResourceRange range;
	};

	enum class LoadOperation : std::uint8_t {
		Load,
		Clear,
		Discard
	};

	enum class StoreOperation : std::uint8_t {
		Store,
		Discard
	};

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

	struct BeginRendering {
		RenderArea area;
		std::vector<ColorAttachment> colors;
		std::optional<DepthStencilAttachment> depth_stencil;
	};

	struct EndRendering {};

	struct BindPipeline {
		std::size_t pipeline;
	};

	struct BindResourceGroup {
		std::size_t group;
		std::uint32_t index = 0u;
	};

	struct BindVertexBuffer {
		std::size_t resource;
		std::uint32_t slot = 0u;
		std::uint32_t stride = 0u;
		std::size_t offset = 0u;
	};

	enum class IndexType : std::uint8_t {
		Uint16,
		Uint32
	};

	struct BindIndexBuffer {
		std::size_t resource;
		IndexType type = IndexType::Uint16;
		std::size_t offset = 0u;
	};

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

	struct Scissor {
		std::int32_t x = 0;
		std::int32_t y = 0;
		std::uint32_t width = 0u;
		std::uint32_t height = 0u;
	};

	struct Draw {
		std::uint32_t vertex_count = 0u;
		std::uint32_t instance_count = 1u;
		std::uint32_t first_vertex = 0u;
		std::uint32_t first_instance = 0u;
	};

	struct DrawIndexed {
		std::uint32_t index_count = 0u;
		std::uint32_t instance_count = 1u;
		std::uint32_t first_index = 0u;
		std::int32_t vertex_offset = 0;
		std::uint32_t first_instance = 0u;
	};

	struct Dispatch {
		std::uint32_t group_count_x = 1u;
		std::uint32_t group_count_y = 1u;
		std::uint32_t group_count_z = 1u;
	};

	struct CopyBuffer {
		std::size_t source;
		std::size_t destination;
		std::size_t source_offset = 0u;
		std::size_t destination_offset = 0u;
		std::size_t size = 0u;
	};

	struct CopyBufferToTexture {
		std::size_t source;
		std::size_t destination;
		TextureDataLayout source_layout;
		TextureRegion destination_region;
	};

	struct CopyTextureToBuffer {
		std::size_t source;
		std::size_t destination;
		TextureRegion source_region;
		TextureDataLayout destination_layout;
	};

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
			return inner_texture &&
				outer_texture.base_mip_level <= inner_texture->base_mip_level &&
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
	
		bool SameBarrierTransition(ExecutionBarrier const& lhs, ExecutionBarrier const& rhs) noexcept {
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
	
		std::size_t TakeReadyNode(
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
	
		ExecutionPlan CompileExecutionPlan(ExecutionGraph const& graph) {
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

#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
		using CompletionTask = std::move_only_function<bool()>;

		template <class Function>
		CompletionTask MakeCompletionTask(Function&& function) {
			return CompletionTask(std::forward<Function>(function));
		}
#else
		using CompletionTask = std::function<bool()>;

		template <class Function>
		CompletionTask MakeCompletionTask(Function&& function) {
			// C++20 std::function requires a copy-constructible target. Keep the
			// move-only lambda in one shared allocation and copy only this small wrapper.
			auto state = std::make_shared<std::remove_cvref_t<Function>>(
				std::forward<Function>(function)
			);
			return [state = std::move(state)]() mutable {
				return (*state)();
			};
		}
#endif

		void EnqueueCompletion(CompletionTask&& task) {
			static std::deque<CompletionTask> tasks;
			static std::mutex mutex;
			static std::condition_variable condition;
			static std::jthread worker(
				[](std::stop_token stop_token) {
					std::deque<CompletionTask> current;
					std::deque<CompletionTask> pending;
					while (!stop_token.stop_requested()) {
						{
							std::unique_lock<std::mutex> lock(mutex);
							condition.wait_for(
								lock,
								std::chrono::milliseconds(1u)
							);
							current.swap(tasks);
						}
						if (stop_token.stop_requested()) {
							break;
						}
						while (!current.empty()) {
							auto current_task = std::move(current.front());
							current.pop_front();
							if (!current_task()) {
								pending.emplace_back(std::move(current_task));
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
				}
			);
			{
				std::unique_lock<std::mutex> lock(mutex);
				tasks.emplace_back(std::move(task));
			}
			condition.notify_one();
		}

	}

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

	class CompletionToken {
	public:
		using UniqueHandle = std::unique_ptr<
			struct CompletionTokenImplementation,
			void(*)(struct CompletionTokenImplementation*)
		>;

	private:
		UniqueHandle m_impl;

	public:
		CompletionToken() noexcept
			: m_impl(nullptr, nullptr) {
		}

		explicit CompletionToken(UniqueHandle&& impl) noexcept
			: m_impl(std::move(impl)) {
		}

		bool Poll() noexcept;

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

	class CommandGraphResources {
	private:
		std::vector<Resource> m_resources;
		std::vector<View> m_views;
		std::vector<Sampler> m_samplers;
		std::vector<Pipeline> m_pipelines;
		std::vector<PipelineResourceGroup> m_resource_groups;

		template <class Receiver>
		friend class CommandGraphBindings;

		template <class Value>
		static Value Take(std::vector<Value>& values, std::size_t index) {
			if (index >= values.size()) {
				throw std::out_of_range(
					"Command graph result index is out of range"
				);
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
		) noexcept
			: m_resources(std::move(resources)),
			m_views(std::move(views)),
			m_samplers(std::move(samplers)),
			m_pipelines(std::move(pipelines)),
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
	template <class Receiver> concept CommandGraphReceiver = std::move_constructible<Receiver> &&
		requires(
			Receiver& receiver,
			Receiver&& completed_receiver,
			CommandGraphResources&& resources,
			std::exception_ptr error
		) {
			{ receiver.RecoverBindings(std::move(resources)) } noexcept;
			std::execution::get_env(receiver);
			{ std::execution::set_value(
				std::move(completed_receiver),
				std::move(resources)
			) } noexcept;
			{ std::execution::set_error(
				std::move(completed_receiver),
				error
			) } noexcept;
			{ std::execution::set_stopped(
				std::move(completed_receiver)
			) } noexcept;
		};
#else
	template <class Receiver> concept CommandGraphReceiver = std::move_constructible<Receiver> &&
		requires(
			Receiver& receiver,
			Receiver&& completed_receiver,
			CommandGraphResources&& resources,
			std::exception_ptr error
		) {
			receiver.get_env();
			{ receiver.RecoverBindings(std::move(resources)) } noexcept;
			{ std::move(completed_receiver).set_value(
				std::move(resources)
			) } noexcept;
			{ std::move(completed_receiver).set_error(error) } noexcept;
			{ std::move(completed_receiver).set_stopped() } noexcept;
		};
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

	class CommandGraphBuilder {
	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using sender_concept = std::execution::sender_t;
		using completion_signatures = std::execution::completion_signatures<
			std::execution::set_value_t(CommandGraphResources),
			std::execution::set_error_t(std::exception_ptr),
			std::execution::set_stopped_t()
		>;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
	private:
		friend class CommandScheduler;

		std::shared_ptr<CommandSchedulerContext> m_context;
		ExecutionGraph m_graph;

		explicit CommandGraphBuilder(std::shared_ptr<CommandSchedulerContext> const& context) noexcept
			: m_context(context),
			m_graph() {
		}

		ExecutionNode& GetNode(std::size_t id) {
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

		[[nodiscard]] Node CreateNode(QueueType queue = QueueType::Graphics, std::optional<Node> const& dependency = std::nullopt) {
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
			requires CommandGraphReceiver<std::remove_cvref_t<Receiver>>
		CommandGraphBindings<std::remove_cvref_t<Receiver>> connect(Receiver&& receiver) && {
			return { CommandScheduler{ m_context }, details::CompileExecutionPlan(m_graph), std::forward<Receiver>(receiver) };
		}
	};

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

		explicit CommandScheduler(std::shared_ptr<CommandSchedulerContext> const& impl) noexcept
			: m_impl(impl) {
		}

		CommandGraphBuilder schedule() const noexcept {
			return CommandGraphBuilder{ m_impl };
		}
	};

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
			std::execution::set_value(
				std::move(context->receiver),
				std::move(resources)
			);
#else
			std::move(context->receiver).set_value(
				std::move(resources)
			);
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

		static void SetError(std::unique_ptr<OperationStateContext>&& context, std::exception_ptr&& error) noexcept {
			context->receiver.RecoverBindings(
				ReleaseBindings(*context)
			);
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_error(
				std::move(context->receiver),
				std::move(error)
			);
#else
			std::move(context->receiver).set_error(std::move(error));
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

		static void SetStopped(std::unique_ptr<OperationStateContext>&& context) noexcept {
			context->receiver.RecoverBindings(
				ReleaseBindings(*context)
			);
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_stopped(std::move(context->receiver));
#else
			std::move(context->receiver).set_stopped();
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

	public:
		CommandGraphBindings(CommandScheduler const& scheduler, ExecutionPlan&& plan, Receiver&& receiver)
			: m_context(std::make_unique<OperationStateContext>(scheduler, std::move(plan), std::move(receiver))) {

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
					details::EnqueueCompletion(
						details::MakeCompletionTask(
							[operation = std::move(m_context), token = std::move(token)]() mutable noexcept {
								if (!token.Poll()) {
									return false;
								}
								if (auto error = token.Error()) {
									SetError(std::move(operation), std::move(error));
								}
								else if (token.IsStopped()) {
									SetStopped(std::move(operation));
								}
								else {
									SetValue(std::move(operation));
								}
								return true;
							}
						)
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
	};
} // namespace fyuu_rhi::execution
