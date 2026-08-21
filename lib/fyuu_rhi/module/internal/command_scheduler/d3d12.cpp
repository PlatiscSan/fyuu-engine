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
#endif

module fyuu_rhi:d3d12_command_scheduler;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif
import :command_scheduler_dispatch;
import :completion_token_dispatch;
import :completion_token_factory;
import :d3d12_data;
import :d3d12_utility;
import :execution;
import :pipeline_factory;
import :pipeline_resource_group_factory;
import :resource_factory;
import :sampler_factory;
import :view_factory;

namespace fyuu_rhi::details {
	/// Implemented in an ordinary source file so TBB headers never enter this
	/// module partition. MSVC 14.51 (CL 19.51) otherwise emits C1116 while an
	/// importer loads module std.
	extern "C" void ParallelFor(
		std::size_t first,
		std::size_t last,
		void* function,
		void (*invoke)(void*, std::size_t)
	);
}

namespace {
	using namespace fyuu_rhi::d3d12;
	using namespace fyuu_rhi::execution;

	using fyuu_rhi::TextureDataLayout;
	using fyuu_rhi::TextureRegion;

	template <class Function>
	void ParallelFor(
		std::size_t first,
		std::size_t last,
		Function&& function
	) {
		fyuu_rhi::details::ParallelFor(
			first,
			last,
			std::addressof(function),
			[](void* erased_function, std::size_t index) {
				(*static_cast<std::remove_reference_t<Function>*>(erased_function))(
					index
				);
			}
		);
	}

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

	D3D12_RESOURCE_STATES StableState(
		fyuu_rhi::ResourceFlags const& flags
	) noexcept {
		using Bits = fyuu_rhi::ResourceFlagBits;
		if (flags.Test(Bits::HostVisible)) {
			return D3D12_RESOURCE_STATE_GENERIC_READ;
		}
		if (flags.Test(Bits::DeviceReadback)) {
			return D3D12_RESOURCE_STATE_COPY_DEST;
		}
		return D3D12_RESOURCE_STATE_COMMON;
	}

	/// Unwraps D3D12MA only at the native API boundary.
	ID3D12Resource* NativeResource(std::span<std::reference_wrapper<Resource> const> resources, std::size_t index) {
		return resources[index].get().allocation->GetResource();
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
		if constexpr (std::same_as<Resource, fyuu_rhi::d3d12::Resource>) {
			native_resource = resource.allocation->GetResource();
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
	QueueContext::ManagedCommandList AcquireCommands(std::shared_ptr<QueueContext> const& queue) {
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
		return QueueContext::ManagedCommandList(
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
		std::shared_ptr<QueueContext> const& queue,
		HWND target,
		Resource const& source,
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

	D3D12_CPU_DESCRIPTOR_HANDLE NativeDescriptor(
		View const& view,
		View::Type type
	) {
		return view.descriptors[static_cast<std::size_t>(type)].CPU();
	}

	bool CoversWholeMip(
		RenderArea const& area,
		Resource const& resource,
		View const& view
	) noexcept {
		auto resource_desc = resource.allocation->GetResource()->GetDesc();
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
		Resource const& resource,
		View const& view,
		D3D12_RECT const& rect
	) {
		auto resource_desc = resource.allocation->GetResource()->GetDesc();
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
					resource.allocation->GetResource(),
					&region
				);
			}
		}
	}

	void ResolveView(
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> const& commands,
		Resource const& source,
		View const& source_view,
		Resource const& destination,
		View const& destination_view
	) {
		if (source_view.mip_level_count != destination_view.mip_level_count ||
			source_view.array_layer_count != destination_view.array_layer_count) {
			throw std::invalid_argument("D3D12 resolve views have different subresource ranges");
		}
		auto source_desc = source.allocation->GetResource()->GetDesc();
		auto destination_desc = destination.allocation->GetResource()->GetDesc();
		for (std::uint32_t layer = 0u; layer < source_view.array_layer_count; ++layer) {
			for (std::uint32_t mip = 0u; mip < source_view.mip_level_count; ++mip) {
				commands->ResolveSubresource(
					destination.allocation->GetResource(),
					NativeSubresource(
						destination_desc,
						destination_view.base_mip_level + mip,
						destination_view.base_array_layer + layer
					),
					source.allocation->GetResource(),
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

		std::span<std::reference_wrapper<Resource> const> resources;
		std::span<std::reference_wrapper<View> const> views;
		std::span<std::reference_wrapper<Pipeline> const> pipelines;
		std::span<std::reference_wrapper<PipelineResourceGroup> const> groups;
		Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> const& commands;
		std::vector<PresentationWork>* presentations;
		std::size_t presentation_index = 0u;
		Pipeline const* pipeline = nullptr;
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
					NativeDescriptor(view, View::Type::RenderTarget)
				);
				if (color.load == LoadOperation::Clear) {
					float clear[] = { color.clear.red, color.clear.green, color.clear.blue, color.clear.alpha };
					auto whole_mip = CoversWholeMip(
						value.area,
						resources[color.resource].get(),
						view
					);
					commands->ClearRenderTargetView(
						NativeDescriptor(view, View::Type::RenderTarget),
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
				depth = NativeDescriptor(view, View::Type::DepthStencil);
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
		QueueContext::ManagedCommandList commands;
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

		PreparedBatch(
			ExecutionBatch const* plan,
			QueueContext::ManagedCommandList&& commands
		) noexcept
			: plan(plan),
			commands(std::move(commands)) {
		}
	};

	/// Creates and fully validates a flip-model swapchain matching the source texture.
	QueueContext::PresentationContext::SwapChain CreateSwapChain(
		std::shared_ptr<QueueContext> const& queue,
		HWND window,
		Resource const& source,
		std::uint32_t buffer_count
	) {
		constexpr std::uint32_t MinimumFlipModelBufferCount = 2u;
		constexpr DXGI_SAMPLE_DESC FlipModelSampling = { 1u, 0u };
		if (!window || buffer_count < MinimumFlipModelBufferCount) {
			throw std::invalid_argument("D3D12 presentation target or buffer count is invalid");
		}
		auto resource_desc = source.allocation->GetResource()->GetDesc();
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
		auto tearing_supported = IsTearingSupported(factory);

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

		QueueContext::PresentationContext::SwapChain result;
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
		std::shared_ptr<QueueContext> const& queue,
		QueueContext::PresentationContext::SwapChain& swapchain,
		Resource const& source,
		std::uint32_t buffer_count
	) {
		DXGI_SWAP_CHAIN_DESC1 swapchain_desc{};
		ThrowIfFailed(
			swapchain.impl->GetDesc1(&swapchain_desc)
		);
		auto source_desc = source.allocation->GetResource()->GetDesc();
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

	/// Acquires the swapchain frame used by one presentation command.
	PresentationWork AcquirePresentation(
		std::shared_ptr<QueueContext> const& queue,
		HWND target,
		Resource const& source,
		std::uint32_t buffer_count,
		bool vertical_sync
	) {
		if (!queue->presentation_context) {
			throw std::invalid_argument("D3D12 presentation requires a graphics queue");
		}

		std::uint64_t reusable_after = 0u;
		PresentationWork result;
		std::unique_lock<std::mutex> presentation_lock(
			queue->presentation_context->mutex
		);
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
		presentation_lock.unlock();
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
		std::span<std::reference_wrapper<Resource> const> resources,
		std::span<D3D12_RESOURCE_STATES const> stable_states,
		std::span<std::reference_wrapper<View> const> views,
		std::span<std::reference_wrapper<Pipeline> const> pipelines,
		std::span<std::reference_wrapper<PipelineResourceGroup> const> groups
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
							stable_states[resource],
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
							stable_states[resource]
						);
					}
				}
			}
		}
		ThrowIfFailed(commands->Close());
		prepared.commands.is_open = false;
	}

}

namespace fyuu_rhi::execution {
	template <>
	struct ExecuteCommands<d3d12::CommandSchedulerContext> {
		d3d12::CommandSchedulerContext* context;

		CompletionToken operator()(
			ExecutionPlan const& plan,
			std::span<PlatformHandle const> presentation_targets,
			std::span<Resource const> bound_resources,
			std::span<View const> bound_views,
			std::span<Sampler const> bound_samplers,
			std::span<Pipeline const> bound_pipelines,
			std::span<PipelineResourceGroup const> bound_resource_groups,
			StopTokenView stop_token
		) const {
		auto const& scheduler = *context;
		std::vector<std::reference_wrapper<d3d12::Resource>> resources;
		std::vector<D3D12_RESOURCE_STATES> stable_states;
		std::vector<std::reference_wrapper<d3d12::View>> views;
		std::vector<std::reference_wrapper<d3d12::Sampler>> samplers;
		std::vector<std::reference_wrapper<d3d12::Pipeline>> pipelines;
		std::vector<std::reference_wrapper<d3d12::PipelineResourceGroup>> resource_groups;
		resources.reserve(bound_resources.size());
		stable_states.reserve(bound_resources.size());
		views.reserve(bound_views.size());
		samplers.reserve(bound_samplers.size());
		pipelines.reserve(bound_pipelines.size());
		resource_groups.reserve(bound_resource_groups.size());
		std::ranges::transform(
			bound_resources,
			std::back_inserter(resources),
			[](Resource const& resource) -> d3d12::Resource& {
				if (!resource.m_impl) {
					throw std::invalid_argument("A D3D12 execution resource is empty");
				}
				auto native = std::get_if<d3d12::Resource>(
					&resource.m_impl->native
				);
				if (!native) {
					throw std::invalid_argument("A D3D12 execution resource uses another backend");
				}
				return *native;
			}
		);
		std::ranges::transform(
			bound_resources,
			std::back_inserter(stable_states),
			[](Resource const& resource) {
				return StableState(resource.m_impl->flags);
			}
		);
		std::ranges::transform(
			bound_views,
			std::back_inserter(views),
			[](View const& view) -> d3d12::View& {
				if (!view.m_impl) {
					throw std::invalid_argument("A D3D12 execution view is empty");
				}
				auto native = std::get_if<d3d12::View>(&view.m_impl->native);
				if (!native) {
					throw std::invalid_argument("A D3D12 execution view uses another backend");
				}
				return *native;
			}
		);
		std::ranges::transform(
			bound_samplers,
			std::back_inserter(samplers),
			[](Sampler const& sampler) -> d3d12::Sampler& {
				if (!sampler.m_impl) {
					throw std::invalid_argument("A D3D12 execution sampler is empty");
				}
				auto native = std::get_if<d3d12::Sampler>(&sampler.m_impl->native);
				if (!native) {
					throw std::invalid_argument("A D3D12 execution sampler uses another backend");
				}
				return *native;
			}
		);
		std::ranges::transform(
			bound_pipelines,
			std::back_inserter(pipelines),
			[](Pipeline const& pipeline) -> d3d12::Pipeline& {
				if (!pipeline.m_impl) {
					throw std::invalid_argument("A D3D12 execution pipeline is empty");
				}
				auto native = std::get_if<d3d12::Pipeline>(&pipeline.m_impl->native);
				if (!native) {
					throw std::invalid_argument("A D3D12 execution pipeline uses another backend");
				}
				return *native;
			}
		);
		std::ranges::transform(
			bound_resource_groups,
			std::back_inserter(resource_groups),
			[](PipelineResourceGroup const& group) -> d3d12::PipelineResourceGroup& {
				if (!group.m_impl) {
					throw std::invalid_argument("A D3D12 execution resource group is empty");
				}
				auto native = std::get_if<d3d12::PipelineResourceGroup>(
					&group.m_impl->native
				);
				if (!native) {
					throw std::invalid_argument("A D3D12 execution resource group uses another backend");
				}
				return *native;
			}
		);
		// Phase 1a validates the complete dynamic plan before consulting stop. This keeps
		// parameter errors deterministic: cancellation cannot hide a malformed later batch.
		if (resources.size() != plan.bindings.resource_count || views.size() != plan.bindings.view_count ||
			samplers.size() != plan.bindings.sampler_count ||
			pipelines.size() != plan.bindings.pipeline_count ||
			resource_groups.size() != plan.bindings.resource_group_count) {
			throw std::invalid_argument("D3D12 execution binding count mismatch");
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
			auto queue = scheduler.queues.find(batch.queue);
			if (queue == scheduler.queues.end() || !queue->second) {
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
		// Phase 1b performs allocation and presentation reservation serially. Failures
		// escape on this calling thread; stop returns every list acquired so far.
		std::deque<PreparedBatch> prepared;
		for (std::size_t index = 0u; index < plan.batches.size(); ++index) {
			if (stop_token.stop_requested()) {
				d3d12::CompletionToken stopped{
					{},
					{},
					true
				};
				for (auto& batch : prepared) {
					stopped.command_lists.emplace_back(std::move(batch.commands));
				}
				return MakeCompletionToken(std::move(stopped));
			}
			auto const& batch = plan.batches[index];
			auto queue = scheduler.queues.find(batch.queue);
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
			prepared.emplace_back(
				&batch,
				AcquireCommands(queue->second)
			);

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
			d3d12::CompletionToken stopped{
				{},
				{},
				true
			};
			for (auto& batch : prepared) {
				stopped.command_lists.emplace_back(std::move(batch.commands));
			}
			return MakeCompletionToken(std::move(stopped));
		}

		// Phase 2 records batches independently. A failing list may still reference bound
		// resources, so it is destroyed here while those bindings are alive and never pooled.
		std::atomic_bool cancelled = false;
		ParallelFor(
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
						stable_states,
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

		d3d12::CompletionToken token{};

		for (auto const& batch : prepared) {
			if (batch.recording_error) {
				token.exception = batch.recording_error;
				for (auto& current : prepared) {
					token.command_lists.emplace_back(std::move(current.commands));
				}
				return MakeCompletionToken(std::move(token));
			}
		}
		if (cancelled.load(std::memory_order_acquire) || stop_token.stop_requested()) {
			token.is_cancelled = true;
			for (auto& batch : prepared) {
				token.command_lists.emplace_back(std::move(batch.commands));
			}
			return MakeCompletionToken(std::move(token));
		}

		// Phase 3 uses one worker per native queue and intentionally ignores stop: once
		// submission starts, every dependency and lifetime fence must be made observable.
		ParallelFor(
			std::size_t{ 0u },
			queue_submissions.size(),
			[&](std::size_t queue_index) {
				auto& submission = queue_submissions[queue_index];
				auto const& queue = submission.queue;
				for (std::size_t position = 0u; position < submission.batches.size(); ++position) {
					auto batch_index = submission.batches[position];
					auto& batch = prepared[batch_index];
					std::unique_lock<std::mutex> submission_lock(
						queue->submission_mutex,
						std::defer_lock
					);
					try {
						// Dependency publication is CPU-side bookkeeping. Never wait for it
						// while owning a native queue lock: concurrent graphs may reach the
						// same pair of queues in opposite orders.
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
								throw std::runtime_error(
									"D3D12 batch dependency was not submitted"
								);
							}
						}
						// D3D12 queue calls are thread-safe individually, but this batch's
						// Wait/Execute/Signal sequence must not interleave with another
						// ExecuteCommands call targeting the same native queue. Fence values
						// are allocated under the same lock so signal order stays monotonic.
						submission_lock.lock();
						auto fence_count = batch.presentations.empty() ? 1u : 2u;
						batch.reserved_fence = queue->next_fence_value;
						queue->next_fence_value += fence_count;
						if (!batch.presentations.empty()) {
							batch.presentation_fence = batch.reserved_fence + 1u;
						}
						for (auto dependency : batch.plan->dependencies) {
							auto& source = prepared[dependency];
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
							std::unique_lock<std::mutex> presentation_lock(
								queue->presentation_context->mutex
							);
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
			return MakeCompletionToken(std::move(token));
		}
	};
}
#endif
