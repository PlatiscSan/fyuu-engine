module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstring>

#include <exception>
#include <stdexcept>

#include <algorithm>
#include <limits>

#include <memory>

#include <deque>
#include <vector>

#include <functional>

#include <cstdint>
#include <utility>

#include <atomic>
#include <mutex>

#include <unordered_map>
#include <unordered_set>

#include <optional>
#include <variant>

#include <compare>
#include <span>

#include <ranges>

#include <format>
#endif
#if defined(_WIN32)
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <tbb/parallel_for.h>
#endif

module fyuu_rhi:d3d12_execution;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif
import :d3d12_traits;
import :d3d12_utility;
import :execution_types;

namespace {
	using namespace fyuu_rhi::d3d12;
	using namespace fyuu_rhi::execution;

	using fyuu_rhi::d3d12::Backend;
	using fyuu_rhi::TextureDataLayout;
	using fyuu_rhi::TextureRegion;

	/// D3D12 reports device removal through the all-ones fence completion sentinel.
	constexpr std::uint64_t FailedFence = (std::numeric_limits<std::uint64_t>::max)();

	/// Maps abstract queue roles to the command-list type required by D3D12.
	/// Graphics and Present intentionally alias a direct queue.
	D3D12_COMMAND_LIST_TYPE NativeQueueType(QueueType type) {
		switch (type) {
		case QueueType::Graphics:
		case QueueType::Present: return D3D12_COMMAND_LIST_TYPE_DIRECT;
		case QueueType::Compute: return D3D12_COMMAND_LIST_TYPE_COMPUTE;
		case QueueType::Transfer: return D3D12_COMMAND_LIST_TYPE_COPY;
		}
		throw std::invalid_argument("Unknown D3D12 queue type");
	}

	/// Converts an execution usage/mode pair into its stable native resource state.
	D3D12_RESOURCE_STATES NativeState(ResourceUsage usage, AccessMode mode) {
		if (HasUsage(usage, ResourceUsage::PresentationSource)) {
			return D3D12_RESOURCE_STATE_COPY_SOURCE;
		}
		if (HasUsage(usage, ResourceUsage::ColorAttachment)) {
			return D3D12_RESOURCE_STATE_RENDER_TARGET;
		}
		if (HasUsage(usage, ResourceUsage::DepthStencilAttachment)) {
			return mode == AccessMode::Read ? D3D12_RESOURCE_STATE_DEPTH_READ : D3D12_RESOURCE_STATE_DEPTH_WRITE;
		}
		if (HasUsage(usage, ResourceUsage::CopyDestination)) {
			return D3D12_RESOURCE_STATE_COPY_DEST;
		}
		if (HasUsage(usage, ResourceUsage::CopySource)) {
			return D3D12_RESOURCE_STATE_COPY_SOURCE;
		}
		if (HasUsage(usage, ResourceUsage::ResolveDestination)) {
			return D3D12_RESOURCE_STATE_RESOLVE_DEST;
		}
		if (HasUsage(usage, ResourceUsage::ResolveSource)) {
			return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
		}
		if (HasUsage(usage, ResourceUsage::Storage)) {
			return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		}
		if (HasUsage(usage, ResourceUsage::IndexBuffer)) {
			return D3D12_RESOURCE_STATE_INDEX_BUFFER;
		}
		if (HasUsage(usage, ResourceUsage::Indirect)) {
			return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
		}

		D3D12_RESOURCE_STATES result = D3D12_RESOURCE_STATE_COMMON;
		if (HasUsage(usage, ResourceUsage::VertexBuffer) || HasUsage(usage, ResourceUsage::Uniform)) {
			result |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		}
		if (HasUsage(usage, ResourceUsage::Sampled)) {
			result |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
		}
		return result;
	}

	/// Unwraps D3D12MA only at the native API boundary.
	ID3D12Resource* NativeResource(std::span<std::reference_wrapper<Backend::Resource> const> resources, std::size_t index) {
		return resources[index].get().impl->GetResource();
	}

	/// Emits the minimum barrier for either an engine resource or a swapchain ComPtr.
	/// Read-only composites and D3D12 implicit copy promotion are preserved.
	template<typename Resource>
	void Transition(
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> const& commands,
		Resource const& resource,
		D3D12_RESOURCE_STATES before,
		D3D12_RESOURCE_STATES after
	) {
		ID3D12Resource* native_resource = nullptr;
		if constexpr (std::same_as<Resource, Backend::Resource>) {
			native_resource = resource.impl->GetResource();
		}
		else {
			native_resource = resource.Get();
		}
		constexpr auto WriteStates = D3D12_RESOURCE_STATE_RENDER_TARGET |
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS |
			D3D12_RESOURCE_STATE_DEPTH_WRITE |
			D3D12_RESOURCE_STATE_STREAM_OUT |
			D3D12_RESOURCE_STATE_COPY_DEST |
			D3D12_RESOURCE_STATE_RESOLVE_DEST;
		auto const read_only = (before & WriteStates) == 0u && (after & WriteStates) == 0u;
		if (before == D3D12_RESOURCE_STATE_COMMON &&
			(after == D3D12_RESOURCE_STATE_COPY_SOURCE || after == D3D12_RESOURCE_STATE_COPY_DEST)) {
			// COMMON is implicitly promotable for copy access. In particular this
			// is the required entry state for resources crossing onto a copy queue.
			return;
		}
		if (read_only && (((before & after) == after) || ((before & after) == before))) {
			// GENERIC_READ is a composite state. Upload resources remain in it for
			// their lifetime and already satisfy COPY_SOURCE/vertex/index reads.
			return;
		}
		if (before == after) {
			if (after == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
				D3D12_RESOURCE_BARRIER barrier{};
				barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
				barrier.UAV.pResource = native_resource;
				commands->ResourceBarrier(1u, &barrier);
			}
			return;
		}
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = native_resource;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = before;
		barrier.Transition.StateAfter = after;
		commands->ResourceBarrier(1u, &barrier);
	}

	/// Acquires an open list. Reused lists recover their allocator from private data;
	/// recycling already waited for the previous fence before placing the list in the pool.
	Backend::QueueContext::ManagedCommandList AcquireCommands(std::shared_ptr<Backend::QueueContext> const& queue) {
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
		{
			std::unique_lock<std::mutex> lock(queue->command_lists_mutex);
			if (!queue->command_lists.empty()) {
				commands = std::move(queue->command_lists.front());
				queue->command_lists.pop_front();
			}
		}

		Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
		if (commands) {
			UINT size = sizeof(ID3D12CommandAllocator*);
			ID3D12CommandAllocator* raw_allocator = nullptr;
			ThrowIfFailed(
				commands->GetPrivateData(__uuidof(ID3D12CommandAllocator), &size, &raw_allocator)
			);
			allocator.Attach(raw_allocator);
			ThrowIfFailed(
				allocator->Reset()
			);
			ThrowIfFailed(
				commands->Reset(allocator.Get(), nullptr)
			);
		}
		else {
			auto device = GetLogicalDevice(queue->impl);
			auto type = queue->impl->GetDesc().Type;
			ThrowIfFailed(
				device->CreateCommandAllocator(type, IID_PPV_ARGS(&allocator))
			);
			ThrowIfFailed(
				device->CreateCommandList(
					0u, type, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)
				)
			);
			ThrowIfFailed(
				commands->SetPrivateDataInterface(__uuidof(ID3D12CommandAllocator), allocator.Get())
			);
		}
		return Backend::QueueContext::ManagedCommandList(
			queue,
			std::move(commands),
			0u,
			true
		);
	}

	/// Phase-1 back-buffer reservation consumed by the Present recorder in phase 2.
	struct PresentationWork {
		HWND target = nullptr;
		std::size_t source = 0u;
		Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain;
		Microsoft::WRL::ComPtr<ID3D12Resource> back_buffer;
		std::uint32_t frame_index = 0u;
		bool vertical_sync = true;
		bool tearing_supported = false;
	};

	PresentationWork AcquirePresentation(
		std::shared_ptr<Backend::QueueContext> const& queue,
		HWND target,
		Backend::Resource const& source,
		std::uint32_t buffer_count,
		bool vertical_sync
	);

	D3D12_RECT NativeRect(RenderArea const& area) noexcept {
		return {
			.left = area.x,
			.top = area.y,
			.right = area.x + static_cast<LONG>(area.width),
			.bottom = area.y + static_cast<LONG>(area.height)
		};
	}

	UINT NativeSubresource(
		D3D12_RESOURCE_DESC const& resource,
		std::uint32_t mip_level,
		std::uint32_t array_layer
	) noexcept {
		return mip_level + array_layer * resource.MipLevels;
	}

	bool CoversWholeMip(
		RenderArea const& area,
		Backend::Resource const& resource,
		Backend::View const& view
	) noexcept {
		auto resource_desc = resource.impl->GetResource()->GetDesc();
		auto width = resource_desc.Width >> view.base_mip_level;
		if (width == 0u) {
			width = 1u;
		}
		auto height = resource_desc.Height >> view.base_mip_level;
		if (height == 0u) {
			height = 1u;
		}
		return area.x == 0 && area.y == 0 &&
			area.width == width && area.height == height;
	}

	void DiscardView(
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> const& commands,
		Backend::Resource const& resource,
		Backend::View const& view,
		D3D12_RECT const& rect
	) {
		auto resource_desc = resource.impl->GetResource()->GetDesc();
		for (std::uint32_t layer = 0u; layer < view.array_layer_count; ++layer) {
			for (std::uint32_t mip = 0u; mip < view.mip_level_count; ++mip) {
				D3D12_DISCARD_REGION region{
					.NumRects = 1u,
					.pRects = &rect,
					.FirstSubresource = NativeSubresource(
						resource_desc,
						view.base_mip_level + mip,
						view.base_array_layer + layer
					),
					.NumSubresources = 1u
				};
				commands->DiscardResource(
					resource.impl->GetResource(),
					&region
				);
			}
		}
	}

	void ResolveView(
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> const& commands,
		Backend::Resource const& source,
		Backend::View const& source_view,
		Backend::Resource const& destination,
		Backend::View const& destination_view
	) {
		if (source_view.mip_level_count != destination_view.mip_level_count ||
			source_view.array_layer_count != destination_view.array_layer_count) {
			throw std::invalid_argument("D3D12 resolve views have different subresource ranges");
		}
		auto source_desc = source.impl->GetResource()->GetDesc();
		auto destination_desc = destination.impl->GetResource()->GetDesc();
		for (std::uint32_t layer = 0u; layer < source_view.array_layer_count; ++layer) {
			for (std::uint32_t mip = 0u; mip < source_view.mip_level_count; ++mip) {
				commands->ResolveSubresource(
					destination.impl->GetResource(),
					NativeSubresource(
						destination_desc,
						destination_view.base_mip_level + mip,
						destination_view.base_array_layer + layer
					),
					source.impl->GetResource(),
					NativeSubresource(
						source_desc,
						source_view.base_mip_level + mip,
						source_view.base_array_layer + layer
					),
					destination_view.format
				);
			}
		}
	}

	/// Command visitor. Its mutable state mirrors D3D12 binding/rendering state, while
	/// Present commands consume reservations prepared serially before recording starts.
	struct Recorder {

		std::span<std::reference_wrapper<Backend::Resource> const> resources;
		std::span<std::reference_wrapper<Backend::View> const> views;
		std::span<std::reference_wrapper<Backend::Pipeline> const> pipelines;
		std::span<std::reference_wrapper<Backend::PipelineResourceGroup> const> groups;
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> const& commands;
		std::vector<PresentationWork>* presentations;
		std::size_t presentation_index = 0u;
		Backend::Pipeline const* pipeline = nullptr;
		std::optional<BeginRendering> rendering;

		void operator()(BeginRendering const& value) {
			auto rect = NativeRect(value.area);
			D3D12_VIEWPORT viewport{
				.TopLeftX = static_cast<float>(value.area.x),
				.TopLeftY = static_cast<float>(value.area.y),
				.Width = static_cast<float>(value.area.width),
				.Height = static_cast<float>(value.area.height),
				.MinDepth = 0.0f,
				.MaxDepth = 1.0f
			};
			commands->RSSetViewports(1u, &viewport);
			commands->RSSetScissorRects(1u, &rect);
			std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> render_targets;
			render_targets.reserve(value.colors.size());
			for (auto const& color : value.colors) {
				auto const& view = views[color.view].get();
				render_targets.emplace_back(
					view.CPU(Backend::View::Type::RenderTarget)
				);
				if (color.load == LoadOperation::Clear) {
					float clear[] = { color.clear.red, color.clear.green, color.clear.blue, color.clear.alpha };
					auto whole_mip = CoversWholeMip(
						value.area,
						resources[color.resource].get(),
						view
					);
					commands->ClearRenderTargetView(
						view.CPU(Backend::View::Type::RenderTarget),
						clear,
						whole_mip ? 0u : 1u,
						whole_mip ? nullptr : &rect
					);
				}
				else if (color.load == LoadOperation::Discard) {
					DiscardView(
						commands,
						resources[color.resource].get(),
						view,
						rect
					);
				}
			}
			D3D12_CPU_DESCRIPTOR_HANDLE depth{};
			D3D12_CPU_DESCRIPTOR_HANDLE* depth_ptr = nullptr;
			if (value.depth_stencil) {
				auto const& view = views[value.depth_stencil->view].get();
				depth = view.CPU(Backend::View::Type::DepthStencil);
				depth_ptr = &depth;
				UINT flags = 0u;
				if (value.depth_stencil->depth_load == LoadOperation::Clear) {
					flags |= D3D12_CLEAR_FLAG_DEPTH;
				}
				if (value.depth_stencil->stencil_load == LoadOperation::Clear) {
					flags |= D3D12_CLEAR_FLAG_STENCIL;
				}
				// Discard precedes clear because DiscardResource drops both aspects:
				// clearing first would let a stencil-only discard wipe the fresh depth.
				if (value.depth_stencil->depth_load == LoadOperation::Discard ||
					value.depth_stencil->stencil_load == LoadOperation::Discard) {
					DiscardView(
						commands,
						resources[value.depth_stencil->resource].get(),
						view,
						rect
					);
				}
				if (flags != 0u) {
					auto whole_mip = CoversWholeMip(
						value.area,
						resources[value.depth_stencil->resource].get(),
						view
					);
					commands->ClearDepthStencilView(
						depth,
						static_cast<D3D12_CLEAR_FLAGS>(flags),
						value.depth_stencil->clear_depth,
						static_cast<UINT8>(value.depth_stencil->clear_stencil),
						whole_mip ? 0u : 1u,
						whole_mip ? nullptr : &rect
					);
				}
			}
			commands->OMSetRenderTargets(
				static_cast<UINT>(render_targets.size()),
				render_targets.data(),
				FALSE,
				depth_ptr
			);
			rendering = value;
		}

		void operator()(EndRendering const&) {
			if (!rendering) {
				throw std::logic_error("D3D12 EndRendering without BeginRendering");
			}
			for (auto const& color : rendering->colors) {
				if (color.resolve_resource) {
					ResolveView(
						commands,
						resources[color.resource].get(),
						views[color.view].get(),
						resources[color.resolve_resource.value()].get(),
						views[color.resolve_view.value()].get()
					);
				}
				if (color.store == StoreOperation::Discard) {
					DiscardView(
						commands,
						resources[color.resource].get(),
						views[color.view].get(),
						NativeRect(rendering->area)
					);
				}
			}
			if (rendering->depth_stencil &&
				(rendering->depth_stencil->depth_store == StoreOperation::Discard ||
					rendering->depth_stencil->stencil_store == StoreOperation::Discard)) {
				DiscardView(
					commands,
					resources[rendering->depth_stencil->resource].get(),
					views[rendering->depth_stencil->view].get(),
					NativeRect(rendering->area)
				);
			}
			rendering.reset();
		}

		void operator()(BindPipeline const& value) {
			pipeline = &pipelines[value.pipeline].get();
			commands->SetPipelineState(pipeline->impl.Get());
			if (pipeline->compute) {
				commands->SetComputeRootSignature(pipeline->root_signature.Get());
			}
			else {
				commands->SetGraphicsRootSignature(pipeline->root_signature.Get());
				commands->IASetPrimitiveTopology(pipeline->primitive_topology);
			}
		}

		void operator()(BindResourceGroup const& value) {
			if (!pipeline) {
				throw std::logic_error("D3D12 resource group requires a bound pipeline");
			}
			auto const& group = groups[value.group].get();
			if (group.root_signature.Get() != pipeline->root_signature.Get()) {
				throw std::invalid_argument("D3D12 resource group root signature mismatch");
			}
			ID3D12DescriptorHeap* heaps[] = { group.resource_heap.Native(), group.sampler_heap.Native() };
			UINT count = 0u;
			ID3D12DescriptorHeap* active[2]{};
			for (auto heap : heaps) {
				if (heap) {
					active[count++] = heap;
				}
			}
			if (count) {
				commands->SetDescriptorHeaps(count, active);
			}
			for (auto const& table : group.tables) {
				if (pipeline->compute) {
					commands->SetComputeRootDescriptorTable(table.root_parameter, table.descriptors.GPU());
				}
				else {
					commands->SetGraphicsRootDescriptorTable(table.root_parameter, table.descriptors.GPU());
				}
			}
		}

		void operator()(BindVertexBuffer const& value) {
			auto resource = NativeResource(resources, value.resource);
			D3D12_VERTEX_BUFFER_VIEW view{
				.BufferLocation = resource->GetGPUVirtualAddress() + value.offset,
				.SizeInBytes = static_cast<UINT>(resource->GetDesc().Width - value.offset),
				.StrideInBytes = value.stride
			};
			commands->IASetVertexBuffers(value.slot, 1u, &view);
		}

		void operator()(BindIndexBuffer const& value) {
			auto resource = NativeResource(resources, value.resource);
			D3D12_INDEX_BUFFER_VIEW view{
				.BufferLocation = resource->GetGPUVirtualAddress() + value.offset,
				.SizeInBytes = static_cast<UINT>(resource->GetDesc().Width - value.offset),
				.Format = value.type == IndexType::Uint16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT
			};
			commands->IASetIndexBuffer(&view);
		}

		void operator()(Viewport const& value) {
			D3D12_VIEWPORT viewport{ value.x, value.y, value.width, value.height,
				value.minimum_depth, value.maximum_depth };
			commands->RSSetViewports(1u, &viewport);
		}

		void operator()(Scissor const& value) {
			D3D12_RECT rect{ value.x, value.y, value.x + static_cast<LONG>(value.width),
				value.y + static_cast<LONG>(value.height) };
			commands->RSSetScissorRects(1u, &rect);
		}

		void operator()(Draw const& value) {
			commands->DrawInstanced(value.vertex_count, value.instance_count, value.first_vertex, value.first_instance);
		}
		void operator()(DrawIndexed const& value) {
			commands->DrawIndexedInstanced(
				value.index_count,
				value.instance_count,
				value.first_index,
				value.vertex_offset,
				value.first_instance
			);
		}
		void operator()(Dispatch const& value) {
			commands->Dispatch(value.group_count_x, value.group_count_y, value.group_count_z);
		}
		void operator()(CopyBuffer const& value) {
			commands->CopyBufferRegion(
				NativeResource(resources, value.destination),
				value.destination_offset,
				NativeResource(resources, value.source),
				value.source_offset,
				value.size
			);
		}
		void operator()(WriteBuffer const& value) {
			auto* resource = NativeResource(resources, value.resource);
			auto desc = resource->GetDesc();
			if (value.offset > desc.Width || value.data.size() > desc.Width - value.offset) {
				throw std::out_of_range("D3D12 write exceeds the buffer size");
			}
			D3D12_RANGE range{ value.offset, value.offset + value.data.size() };
			void* pointer = nullptr;
			auto hr = resource->Map(0u, &range, &pointer);
			if (FAILED(hr)) {
				throw std::runtime_error(
					std::format(
						"D3D12 buffer Map failed: 0x{:08X}", hr
					)
				);
			}
			std::memcpy(pointer, value.data.data(), value.data.size());
			resource->Unmap(0u, &range);
		}

		D3D12_TEXTURE_COPY_LOCATION BufferLocation(std::size_t resource_index, TextureDataLayout const& layout,
			DXGI_FORMAT format, TextureRegion const& region) const {
			D3D12_TEXTURE_COPY_LOCATION result{};
			result.pResource = NativeResource(resources, resource_index);
			result.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			result.PlacedFootprint.Offset = layout.offset;
			result.PlacedFootprint.Footprint = { format, region.width, region.height,
				region.depth * region.array_layer_count, layout.bytes_per_row };
			return result;
		}

		D3D12_TEXTURE_COPY_LOCATION TextureLocation(std::size_t resource_index, TextureRegion const& region) const {
			auto resource = NativeResource(resources, resource_index);
			auto desc = resource->GetDesc();
			D3D12_TEXTURE_COPY_LOCATION result{};
			result.pResource = resource;
			result.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			result.SubresourceIndex = region.mip_level + region.base_array_layer * desc.MipLevels;
			return result;
		}

		void operator()(CopyBufferToTexture const& value) {
			auto destination = NativeResource(resources, value.destination);
			auto source = BufferLocation(
				value.source,
				value.source_layout,
				destination->GetDesc().Format,
				value.destination_region
			);
			auto target = TextureLocation(value.destination, value.destination_region);
			commands->CopyTextureRegion(
				&target,
				value.destination_region.offset_x,
				value.destination_region.offset_y,
				value.destination_region.offset_z,
				&source,
				nullptr
			);
		}
		void operator()(CopyTextureToBuffer const& value) {
			auto source_resource = NativeResource(resources, value.source);
			auto source = TextureLocation(value.source, value.source_region);
			auto target = BufferLocation(
				value.destination,
				value.destination_layout,
				source_resource->GetDesc().Format,
				value.source_region
			);
			D3D12_BOX box{ value.source_region.offset_x, value.source_region.offset_y,
				value.source_region.offset_z, value.source_region.offset_x + value.source_region.width,
				value.source_region.offset_y + value.source_region.height,
				value.source_region.offset_z + value.source_region.depth };
			commands->CopyTextureRegion(&target, 0u, 0u, 0u, &source, &box);
		}
		void operator()(CopyTexture const& value) {
			auto source = TextureLocation(value.source, value.source_region);
			auto target = TextureLocation(value.destination, value.destination_region);
			D3D12_BOX box{ value.source_region.offset_x, value.source_region.offset_y,
				value.source_region.offset_z, value.source_region.offset_x + value.source_region.width,
				value.source_region.offset_y + value.source_region.height,
				value.source_region.offset_z + value.source_region.depth };
			commands->CopyTextureRegion(
				&target,
				value.destination_region.offset_x,
				value.destination_region.offset_y,
				value.destination_region.offset_z,
				&source,
				&box
			);
		}
		void operator()(Present const& value) {
			if (!presentations || presentation_index >= presentations->size()) {
				throw std::logic_error("D3D12 presentation work was not prepared");
			}
			auto& work = (*presentations)[presentation_index++];
			if (work.source != value.source) {
				throw std::logic_error("D3D12 presentation work does not match its command");
			}
			Transition(
				commands,
				work.back_buffer,
				D3D12_RESOURCE_STATE_PRESENT,
				D3D12_RESOURCE_STATE_COPY_DEST
			);
			commands->CopyResource(work.back_buffer.Get(), NativeResource(resources, value.source));
			Transition(
				commands,
				work.back_buffer,
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_PRESENT
			);
		}
	};

	/// Per-batch state spanning preparation, recording, and submission.
	struct PreparedBatch {
		enum class SubmissionState : std::uint8_t {
			Pending,
			Signaled,
			Failed
		};

		/// Non-owning pointer into the caller-owned plan, valid for this execution call.
		ExecutionBatch const* plan = nullptr;
		/// Acquired in phase 1, recorded in phase 2, and submitted in phase 3.
		Backend::QueueContext::ManagedCommandList commands;
		/// Phase-2 failure. Its partially recorded list is destroyed immediately.
		std::exception_ptr recording_error;
		/// Phase-3 failure, inspected later in deterministic batch order.
		std::exception_ptr submission_error;
		/// Publishes whether dependency fences were signaled before another queue waits.
		std::atomic<SubmissionState> submission_state = SubmissionState::Pending;
		/// Fence after ExecuteCommandLists, used by cross-queue dependencies.
		std::uint64_t reserved_fence = 0u;
		/// Fence after Present, used for back-buffer and token retirement.
		std::uint64_t presentation_fence = 0u;
		/// True if ExecuteCommandLists ran before Signal reported a failure.
		bool execution_started = false;
		/// Back-buffer reservations stored in Present command order.
		std::vector<PresentationWork> presentations;
	};

	/// Creates and fully validates a flip-model swapchain matching the source texture.
	Backend::QueueContext::PresentationContext::SwapChain CreateSwapChain(
		std::shared_ptr<Backend::QueueContext> const& queue,
		HWND window,
		Backend::Resource const& source,
		std::uint32_t buffer_count
	) {
		constexpr std::uint32_t MinimumFlipModelBufferCount = 2u;
		constexpr DXGI_SAMPLE_DESC FlipModelSampling = { 1u, 0u };
		if (!window || buffer_count < MinimumFlipModelBufferCount) {
			throw std::invalid_argument("D3D12 presentation target or buffer count is invalid");
		}
		auto resource_desc = source.impl->GetResource()->GetDesc();
		if (resource_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
			resource_desc.DepthOrArraySize != 1u || resource_desc.MipLevels != 1u ||
			resource_desc.SampleDesc.Count != FlipModelSampling.Count ||
			resource_desc.SampleDesc.Quality != FlipModelSampling.Quality ||
			resource_desc.Format == DXGI_FORMAT_UNKNOWN) {
			throw std::invalid_argument("D3D12 presentation source must be a single-sampled 2D texture");
		}

		auto device = GetLogicalDevice(queue->impl);
		auto adapter = GetPhysicalDevice(device);
		auto factory = GetInstance(adapter);
		auto tearing_supported = TearingSupported(factory);

		D3D12_RESOURCE_DESC const& desc = resource_desc;
		DXGI_SWAP_CHAIN_DESC1 swapchain_desc{
			.Width = static_cast<UINT>(desc.Width),
			.Height = desc.Height,
			.Format = desc.Format,
			.Stereo = FALSE,
			.SampleDesc = FlipModelSampling,
			.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
			.BufferCount = buffer_count,
			.Scaling = DXGI_SCALING_STRETCH,
			.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
			.AlphaMode = DXGI_ALPHA_MODE_IGNORE,
			.Flags = tearing_supported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u
		};
		Microsoft::WRL::ComPtr<IDXGISwapChain1> base;
		ThrowIfFailed(
			factory->CreateSwapChainForHwnd(
				queue->impl.Get(), window, &swapchain_desc, nullptr, nullptr, &base
			)
		);
		ThrowIfFailed(
			factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER)
		);

		Backend::QueueContext::PresentationContext::SwapChain result;
		result.tearing_supported = tearing_supported;
		ThrowIfFailed(
			base.As(&result.impl)
		);
		result.back_buffers.resize(buffer_count);
		for (UINT index = 0u; index < buffer_count; ++index) {
			ThrowIfFailed(
				result.impl->GetBuffer(
					index, IID_PPV_ARGS(&result.back_buffers[index].resource)
				)
			);
			result.back_buffers[index].fence_value = 0u;
		}
		return result;
	}

	void ValidateSwapChain(
		std::shared_ptr<Backend::QueueContext> const& queue,
		Backend::QueueContext::PresentationContext::SwapChain& swapchain,
		Backend::Resource const& source,
		std::uint32_t buffer_count
	) {
		DXGI_SWAP_CHAIN_DESC1 swapchain_desc{};
		ThrowIfFailed(
			swapchain.impl->GetDesc1(&swapchain_desc)
		);
		auto source_desc = source.impl->GetResource()->GetDesc();
		if (swapchain_desc.Width != source_desc.Width ||
			swapchain_desc.Height != source_desc.Height ||
			swapchain_desc.Format != source_desc.Format ||
			swapchain_desc.BufferCount != buffer_count) {
			std::uint64_t reusable_after = 0u;
			for (auto const& back_buffer : swapchain.back_buffers) {
				if (back_buffer.fence_value > reusable_after) {
					reusable_after = back_buffer.fence_value;
				}
			}
			if (reusable_after != 0u) {
				WaitForFence(
					queue->fence,
					reusable_after
				);
			}
			swapchain.back_buffers.clear();
			ThrowIfFailed(
				swapchain.impl->ResizeBuffers(
					buffer_count,
					static_cast<UINT>(source_desc.Width),
					source_desc.Height,
					source_desc.Format,
					swapchain.tearing_supported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u
				)
			);
			swapchain.back_buffers.resize(buffer_count);
			for (UINT index = 0u; index < buffer_count; ++index) {
				ThrowIfFailed(
					swapchain.impl->GetBuffer(
						index,
						IID_PPV_ARGS(&swapchain.back_buffers[index].resource)
					)
				);
				swapchain.back_buffers[index].fence_value = 0u;
			}
		}
	}

	/// Reserves the swapchain's current frame inside the scheduler transaction.
	/// SchedulerContext::Implementation::execution_mutex serializes all cache access.
	PresentationWork AcquirePresentation(
		std::shared_ptr<Backend::QueueContext> const& queue,
		HWND target,
		Backend::Resource const& source,
		std::uint32_t buffer_count,
		bool vertical_sync
	) {
		if (!queue->presentation_context) {
			throw std::invalid_argument("D3D12 presentation requires a graphics queue");
		}

		std::uint64_t reusable_after = 0u;
		PresentationWork result;
		if (!queue->presentation_context->cache.Contains(target)) {
			queue->presentation_context->cache.Put(
				target,
				CreateSwapChain(
					queue,
					target,
					source,
					buffer_count
				)
			);
		}
		auto& swapchain = queue->presentation_context->cache.Get(target);
		ValidateSwapChain(
			queue,
			swapchain,
			source,
			buffer_count
		);
		auto frame_index = swapchain.impl->GetCurrentBackBufferIndex();
		if (frame_index >= swapchain.back_buffers.size()) {
			throw std::runtime_error("D3D12 swapchain returned an invalid frame index");
		}
		auto& frame = swapchain.back_buffers[frame_index];
		result = {
			.target = target,
			.swapchain = swapchain.impl,
			.back_buffer = frame.resource,
			.frame_index = frame_index,
			.vertical_sync = vertical_sync,
			.tearing_supported = swapchain.tearing_supported
		};
		reusable_after = frame.fence_value;
		if (reusable_after != 0u) {
			fyuu_rhi::d3d12::WaitForFence(queue->fence, reusable_after);
		}
		return result;
	}

	/// Records one batch without touching scheduler-global mutable state. All dynamic
	/// resources and presentation frames were validated/reserved during phase 1.
	void RecordBatch(
		PreparedBatch& prepared,
		ExecutionPlan const& plan,
		std::span<std::reference_wrapper<Backend::Resource> const> resources,
		std::span<std::reference_wrapper<Backend::View> const> views,
		std::span<std::reference_wrapper<Backend::Pipeline> const> pipelines,
		std::span<std::reference_wrapper<Backend::PipelineResourceGroup> const> groups
	) {
		auto const& commands = prepared.commands.impl;
		Recorder recorder{
			resources,
			views,
			pipelines,
			groups,
			commands,
			&prepared.presentations
		};
		for (auto const& node : prepared.plan->nodes) {
			for (std::size_t resource = 0u; resource < plan.first_accesses.size(); ++resource) {
				for (auto const& access : plan.first_accesses[resource]) {
					if (access.node == node.id) {
						Transition(
							commands,
							resources[resource].get(),
							resources[resource].get().stable_state,
							NativeState(access.usage, access.mode)
						);
					}
				}
			}
			for (auto const& barrier : prepared.plan->barriers) {
				if (barrier.destination_node != node.id) {
					continue;
				}
				auto before = barrier.CrossQueue() ? D3D12_RESOURCE_STATE_COMMON :
					NativeState(barrier.source_usage, barrier.source_mode);
				Transition(
					commands,
					resources[barrier.resource].get(),
					before,
					NativeState(barrier.destination_usage, barrier.destination_mode)
				);
			}
			for (auto const& command : node.commands) {
				std::visit(recorder, command);
			}
			for (auto const& barrier : prepared.plan->release_barriers) {
				if (barrier.source_node == node.id) {
					Transition(
						commands,
						resources[barrier.resource].get(),
						NativeState(barrier.source_usage, barrier.source_mode),
						D3D12_RESOURCE_STATE_COMMON
					);
				}
			}
			for (std::size_t resource = 0u; resource < plan.last_accesses.size(); ++resource) {
				for (auto const& access : plan.last_accesses[resource]) {
					if (access.node != node.id) {
						continue;
					}
					bool released = std::ranges::any_of(prepared.plan->release_barriers, [&](auto const& barrier) {
						return barrier.resource == resource && barrier.source_node == node.id;
						});
					if (!released) {
						Transition(
							commands,
							resources[resource].get(),
							NativeState(access.usage, access.mode),
							resources[resource].get().stable_state
						);
					}
				}
			}
		}
		ThrowIfFailed(commands->Close());
		prepared.commands.is_open = false;
	}

}

namespace fyuu_rhi::d3d12 {
	/// Returns a list to its queue-local pool. Submitted lists wait for their fence;
	/// cancelled open lists are closed first because pooled lists must always be closed.
	Backend::QueueContext::ManagedCommandList::~ManagedCommandList() noexcept {
		try {
			if (!owner || !impl) {
				return;
			}

			if (fence_value != 0u && owner->fence->GetCompletedValue() < fence_value) {
				WaitForFence(owner->fence, fence_value);
			}

			if (is_open) {
				// A cancelled recording can leave the list open. The pool
				// stores closed lists; AcquireCommands owns all Reset operations.
				(void)impl->Close();
			}

			std::unique_lock<std::mutex> lock(owner->command_lists_mutex);
			owner->command_lists.emplace_back(std::move(impl));
		}
		catch (...) {
			// Suppress exceptions in destructor.
		}
	}

	/// Reports completion only after every submitted list reaches its retirement fence.
	/// Device removal is converted into the token's asynchronous exception channel.
	bool Backend::CompletionToken::Poll() noexcept {
		for (auto& commands : command_lists) {
			if (!commands.impl || !commands.owner || commands.fence_value == 0u) {
				continue;
			}
			auto completed = commands.owner->fence->GetCompletedValue();
			if (completed == FailedFence) {
				try {
					auto device = GetLogicalDevice(commands.owner->impl);
					ThrowIfFailed(device->GetDeviceRemovedReason());
				}
				catch (...) {
					if (!exception) {
						exception = std::current_exception();
					}
				}
				commands.owner.reset();
				return true;
			}
			if (completed < commands.fence_value) {
				return false;
			}
		}
		return true;
	}

	std::exception_ptr Backend::CompletionToken::Error() const noexcept { 
		return exception; 
	}

	bool Backend::CompletionToken::IsStopped() const noexcept {
		return is_cancelled; 
	}

	Backend::CompletionToken Backend::ExecuteCommands(
		SchedulerContext const& scheduler,
		ExecutionPlan const& plan,
		std::span<PlatformHandle const> presentation_targets,
		std::span<std::reference_wrapper<Resource> const> resources,
		std::span<std::reference_wrapper<View> const> views,
		std::span<std::reference_wrapper<Sampler> const> samplers,
		std::span<std::reference_wrapper<Pipeline> const> pipelines,
		std::span<std::reference_wrapper<PipelineResourceGroup> const> resource_groups,
		StopTokenView stop_token
	) {
		// Phase 1a validates the complete dynamic plan before consulting stop. This keeps
		// parameter errors deterministic: cancellation cannot hide a malformed later batch.
		if (resources.size() != plan.bindings.resource_count || views.size() != plan.bindings.view_count ||
			samplers.size() != plan.bindings.sampler_count ||
			pipelines.size() != plan.bindings.pipeline_count ||
			resource_groups.size() != plan.bindings.resource_group_count) {
			throw std::invalid_argument("D3D12 execution binding count mismatch");
		}
		if (!scheduler.impl) {
			throw std::invalid_argument("D3D12 scheduler is not initialized");
		}
		struct QueueSubmission {
			/// Native queue shared by all indexed batches in this submission stream.
			std::shared_ptr<QueueContext> queue;
			/// Topologically ordered indices into prepared.
			std::vector<std::size_t> batches;
		};
		// Group by QueueContext rather than QueueType because logical roles may alias.
		std::vector<QueueSubmission> queue_submissions;
		std::unordered_map<std::shared_ptr<QueueContext>, std::size_t> queue_submission_indices;
		// A swapchain frame cannot be reserved twice before either Present advances it.
		std::unordered_set<HWND> active_presentation_targets;
		for (std::size_t index = 0u; index < plan.batches.size(); ++index) {
			auto const& batch = plan.batches[index];
			if (batch.id != index) {
				throw std::invalid_argument("D3D12 execution batch IDs must match storage indices");
			}
			for (auto dependency : batch.dependencies) {
				if (dependency >= index) {
					throw std::invalid_argument("D3D12 execution batches are not topologically ordered");
				}
			}
			auto queue = scheduler.impl->queues.find(batch.queue);
			if (queue == scheduler.impl->queues.end() || !queue->second) {
				throw std::invalid_argument("D3D12 execution plan requests an unavailable queue");
			}
			if (queue->second->impl->GetDesc().Type != NativeQueueType(batch.queue)) {
				throw std::invalid_argument("D3D12 execution queue type mismatch");
			}
			for (auto const& node : batch.nodes) {
				if (node.queue != batch.queue) {
					throw std::invalid_argument("D3D12 execution node and batch queue types differ");
				}
				for (auto const& command : node.commands) {
					if (std::holds_alternative<BeginRendering>(command) &&
						batch.queue != QueueType::Graphics) {
						throw std::invalid_argument("D3D12 BeginRendering requires a graphics queue");
					}
					auto present = std::get_if<Present>(&command);
					if (!present) {
						continue;
					}
					if (present->target >= presentation_targets.size() ||
						present->source >= resources.size() ||
						!presentation_targets[present->target] ||
						!queue->second->presentation_context) {
						throw std::invalid_argument("D3D12 presentation binding is invalid");
					}
					auto target = presentation_targets[present->target];
					if (!active_presentation_targets.emplace(target).second) {
						throw std::invalid_argument(
							"D3D12 execution cannot present one target more than once"
						);
					}
				}
			}
		}
		// One scheduler transaction spans preparation through submission. This protects
		// queue timelines and prevents concurrent calls from reserving the same back buffer.
		std::unique_lock<std::mutex> execution_lock(scheduler.impl->execution_mutex);
		// Phase 1b performs allocation and presentation reservation serially. Failures
		// escape on this calling thread; stop returns every list acquired so far.
		std::deque<PreparedBatch> prepared(plan.batches.size());
		for (std::size_t index = 0u; index < plan.batches.size(); ++index) {
			if (stop_token.stop_requested()) {
				CompletionToken stopped(true);
				for (auto& batch : prepared) {
					stopped.command_lists.emplace_back(std::move(batch.commands));
				}
				return stopped;
			}
			auto const& batch = plan.batches[index];
			auto queue = scheduler.impl->queues.find(batch.queue);
			auto [submission, inserted] = queue_submission_indices.try_emplace(
				queue->second,
				queue_submissions.size()
			);
			if (inserted) {
				queue_submissions.emplace_back(
					QueueSubmission{
						.queue = queue->second
					}
				);
			}
			queue_submissions[submission->second].batches.emplace_back(index);
			prepared[index].plan = &batch;
			prepared[index].commands = AcquireCommands(queue->second);

			for (auto const& node : batch.nodes) {
				for (auto const& command : node.commands) {
					auto present = std::get_if<Present>(&command);
					if (!present) {
						continue;
					}
					auto target = presentation_targets[present->target];
					auto presentation = AcquirePresentation(
						queue->second,
						target,
						resources[present->source].get(),
						present->buffer_count,
						present->vertical_sync
					);
					presentation.source = present->source;
					prepared[index].presentations.emplace_back(std::move(presentation));
				}
			}
		}
		if (stop_token.stop_requested()) {
			CompletionToken stopped(true);
			for (auto& batch : prepared) {
				stopped.command_lists.emplace_back(std::move(batch.commands));
			}
			return stopped;
		}

		// Phase 2 records batches independently. A failing list may still reference bound
		// resources, so it is destroyed here while those bindings are alive and never pooled.
		std::atomic_bool cancelled = false;
		tbb::parallel_for(
			std::size_t{ 0u },
			prepared.size(),
			[&](std::size_t index) {
				if (cancelled.load(std::memory_order_acquire) || stop_token.stop_requested()) {
					cancelled.store(true, std::memory_order_release);
					return;
				}
				try {
					RecordBatch(
						prepared[index],
						plan,
						resources,
						views,
						pipelines,
						resource_groups
					);
				}
				catch (...) {
					prepared[index].recording_error = std::current_exception();
					prepared[index].commands.owner.reset();
					prepared[index].commands.impl.Reset();
				}
			}
		);

		CompletionToken token;

		for (auto const& batch : prepared) {
			if (batch.recording_error) {
				token.exception = batch.recording_error;
				for (auto& current : prepared) {
					token.command_lists.emplace_back(std::move(current.commands));
				}
				return token;
			}
		}
		if (cancelled.load(std::memory_order_acquire) || stop_token.stop_requested()) {
			token.is_cancelled = true;
			for (auto& batch : prepared) {
				token.command_lists.emplace_back(std::move(batch.commands));
			}
			return token;
		}

		// Phase 3 uses one worker per native queue and intentionally ignores stop: once
		// submission starts, every dependency and lifetime fence must be made observable.
		// Reserve all fence values first so cross-queue waits are GPU waits, not CPU waits.
		for (auto& batch : prepared) {
			// Present needs a second point because the command-list fence precedes Present,
			// while the back buffer cannot be retired until work queued after Present completes.
			auto fence_count = batch.presentations.empty() ? 1u : 2u;
			batch.reserved_fence = batch.commands.owner->next_fence_value;
			batch.commands.owner->next_fence_value += fence_count;
			if (!batch.presentations.empty()) {
				batch.presentation_fence = batch.reserved_fence + 1u;
			}
		}
		tbb::parallel_for(
			std::size_t{ 0u },
			queue_submissions.size(),
			[&](std::size_t queue_index) {
				auto& submission = queue_submissions[queue_index];
				auto const& queue = submission.queue;
				for (std::size_t position = 0u; position < submission.batches.size(); ++position) {
					auto batch_index = submission.batches[position];
					auto& batch = prepared[batch_index];
					try {
						for (auto dependency : batch.plan->dependencies) {
							auto& source = prepared[dependency];
							auto source_state = source.submission_state.load(
								std::memory_order_acquire
							);
							while (source_state == PreparedBatch::SubmissionState::Pending) {
								source.submission_state.wait(
									source_state,
									std::memory_order_acquire
								);
								source_state = source.submission_state.load(
									std::memory_order_acquire
								);
							}
							if (source_state == PreparedBatch::SubmissionState::Failed) {
								throw std::runtime_error("D3D12 batch dependency was not submitted");
							}
							if (source.commands.owner == queue) {
								// Submission order already satisfies dependencies within one queue.
								continue;
							}
							ThrowIfFailed(
								queue->impl->Wait(
									source.commands.owner->fence.Get(),
									source.reserved_fence
								)
							);
						}
						ID3D12CommandList* lists[] = { batch.commands.impl.Get() };
						queue->impl->ExecuteCommandLists(1u, lists);
						batch.execution_started = true;
						ThrowIfFailed(
							queue->impl->Signal(
								queue->fence.Get(),
								batch.reserved_fence
							)
						);
						batch.commands.fence_value = batch.reserved_fence;
						batch.submission_state.store(
							PreparedBatch::SubmissionState::Signaled,
							std::memory_order_release
						);
						batch.submission_state.notify_all();
					}
					catch (...) {
						batch.submission_error = std::current_exception();
						batch.submission_state.store(
							PreparedBatch::SubmissionState::Failed,
							std::memory_order_release
						);
						batch.submission_state.notify_all();
						for (++position; position < submission.batches.size(); ++position) {
							auto& skipped = prepared[submission.batches[position]];
							skipped.submission_error = batch.submission_error;
							skipped.submission_state.store(
								PreparedBatch::SubmissionState::Failed,
								std::memory_order_release
							);
							skipped.submission_state.notify_all();
						}
						break;
					}

					for (auto const& presentation : batch.presentations) {
						try {
							ThrowIfFailed(
								presentation.swapchain->Present(
									presentation.vertical_sync ? 1u : 0u,
									!presentation.vertical_sync && presentation.tearing_supported ?
										DXGI_PRESENT_ALLOW_TEARING : 0u
								)
							);
						}
						catch (...) {
							if (!batch.submission_error) {
								batch.submission_error = std::current_exception();
							}
						}
					}
					if (!batch.presentations.empty()) {
						// Signal after every Present so both token completion and frame reuse cover
						// DXGI's references to the back buffer, not merely the preceding copy list.

						try {
							ThrowIfFailed(
								queue->impl->Signal(
									queue->fence.Get(),
									batch.presentation_fence
								)
							);
							batch.commands.fence_value = batch.presentation_fence;
						}
						catch (...) {
							if (!batch.submission_error) {
								batch.submission_error = std::current_exception();
							}
							// Never publish an unsignaled fence into the swapchain cache.
							continue;
						}
						for (auto const& presentation : batch.presentations) {
							auto& cache = queue->presentation_context->cache;
							if (!cache.Contains(presentation.target)) {
								continue;
							}
							auto& cached = cache.Get(presentation.target);
							if (cached.impl.Get() == presentation.swapchain.Get() &&
								presentation.frame_index < cached.back_buffers.size()) {
								cached.back_buffers[presentation.frame_index].fence_value =
									batch.presentation_fence;
							}
						}
					}
				}
			}
		);
		for (auto const& batch : prepared) {
			// Parallel workers write separate batches; plan order chooses a stable error.
			if (batch.submission_error) {
				token.exception = batch.submission_error;
				break;
			}
		}
		for (auto& batch : prepared) {
			if (batch.execution_started &&
				batch.commands.fence_value == 0u) {
				// Submission may have reached ExecuteCommandLists before Signal failed.
				// Such a list cannot safely re-enter the reset/reuse pool.
				batch.commands.owner.reset();
			}
		}
		for (auto& batch : prepared) {
			token.command_lists.emplace_back(std::move(batch.commands));
		}
		return token;
	}
}
#endif
