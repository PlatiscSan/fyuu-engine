module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <array>
#include <atomic>
#include <format>
#include <functional>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <D3D12MemAlloc.h>
#include <dxgi1_5.h>
#include <wrl/client.h>
#endif // defined(_WIN32)
module fyuu_rhi:d3d12_graph_execution;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :d3d12_traits;
import :d3d12_utility;
import :native_command_graph;
namespace {
	using namespace fyuu_rhi;
	using namespace fyuu_rhi::pipeline;
	using namespace fyuu_rhi::d3d12;

	D3D12_RESOURCE_BARRIER TransitionBarrier(
		ID3D12Resource* resource,
		D3D12_RESOURCE_STATES source,
		D3D12_RESOURCE_STATES destination
	) noexcept {
		D3D12_RESOURCE_BARRIER result{};
		result.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		result.Transition.pResource = resource;
		result.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		result.Transition.StateBefore = source;
		result.Transition.StateAfter = destination;
		return result;
	}

	D3D12_RESOURCE_BARRIER UnorderedAccessBarrier(ID3D12Resource* resource) noexcept {
		D3D12_RESOURCE_BARRIER result{};
		result.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		result.UAV.pResource = resource;
		return result;
	}

	bool ResourceStateContains(
		D3D12_RESOURCE_STATES state,
		D3D12_RESOURCE_STATES required
	) noexcept {
		if (required == D3D12_RESOURCE_STATE_COMMON) {
			return state == D3D12_RESOURCE_STATE_COMMON;
		}
		return (state & required) == required;
	}

	std::uint32_t D3D12TextureBlockHeight(DXGI_FORMAT format) noexcept {
		switch (format) {
		case DXGI_FORMAT_BC1_TYPELESS:
		case DXGI_FORMAT_BC1_UNORM:
		case DXGI_FORMAT_BC1_UNORM_SRGB:
		case DXGI_FORMAT_BC2_TYPELESS:
		case DXGI_FORMAT_BC2_UNORM:
		case DXGI_FORMAT_BC2_UNORM_SRGB:
		case DXGI_FORMAT_BC3_TYPELESS:
		case DXGI_FORMAT_BC3_UNORM:
		case DXGI_FORMAT_BC3_UNORM_SRGB:
		case DXGI_FORMAT_BC4_TYPELESS:
		case DXGI_FORMAT_BC4_UNORM:
		case DXGI_FORMAT_BC4_SNORM:
		case DXGI_FORMAT_BC5_TYPELESS:
		case DXGI_FORMAT_BC5_UNORM:
		case DXGI_FORMAT_BC5_SNORM:
		case DXGI_FORMAT_BC6H_TYPELESS:
		case DXGI_FORMAT_BC6H_UF16:
		case DXGI_FORMAT_BC6H_SF16:
		case DXGI_FORMAT_BC7_TYPELESS:
		case DXGI_FORMAT_BC7_UNORM:
		case DXGI_FORMAT_BC7_UNORM_SRGB:
			return 4u;
		default:
			return 1u;
		}
	}

	void ResetCommandEntry(Backend::Scheduler::CommandEntry& entry) {
		if (!entry.closed) {
			ThrowIfFailed(entry.impl->Close());
		}
		ThrowIfFailed(entry.allocator->Reset());
		ThrowIfFailed(entry.impl->Reset(entry.allocator.Get(), nullptr));
		entry.closed = false;
	}

	D3D12_RESOURCE_STATES D3D12ResourceState(execution::GraphAccessFlagBits access) noexcept {
		using Flag = execution::GraphAccessFlagBits;
		if ((access & Flag::Present) != Flag::None) return D3D12_RESOURCE_STATE_COPY_SOURCE;
		if ((access & Flag::ColorAttachment) != Flag::None) return D3D12_RESOURCE_STATE_RENDER_TARGET;
		if ((access & Flag::DepthStencilAttachment) != Flag::None) {
			return (access & Flag::Write) != Flag::None ?
				D3D12_RESOURCE_STATE_DEPTH_WRITE : D3D12_RESOURCE_STATE_DEPTH_READ;
		}
		if ((access & Flag::CopyDestination) != Flag::None) return D3D12_RESOURCE_STATE_COPY_DEST;
		if ((access & Flag::CopySource) != Flag::None) return D3D12_RESOURCE_STATE_COPY_SOURCE;
		if ((access & Flag::ResolveDestination) != Flag::None) return D3D12_RESOURCE_STATE_RESOLVE_DEST;
		if ((access & Flag::ResolveSource) != Flag::None) return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
		if ((access & Flag::Index) != Flag::None) return D3D12_RESOURCE_STATE_INDEX_BUFFER;
		if ((access & Flag::Indirect) != Flag::None) return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
		if ((access & Flag::Storage) != Flag::None) return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
		if ((access & (Flag::Vertex | Flag::Uniform)) != Flag::None) {
			state |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		}
		if ((access & Flag::Sampled) != Flag::None) {
			state |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		}
		return state;
	}

	bool UsesCopyQueueCommonLayout(
		D3D12_COMMAND_LIST_TYPE queue_type,
		D3D12_RESOURCE_STATES current,
		D3D12_RESOURCE_STATES desired
	) noexcept {
		return queue_type == D3D12_COMMAND_LIST_TYPE_COPY &&
			current == D3D12_RESOURCE_STATE_COMMON &&
			(desired == D3D12_RESOURCE_STATE_COPY_SOURCE ||
				desired == D3D12_RESOURCE_STATE_COPY_DEST);
	}

	bool IsTearingSupported(
		Microsoft::WRL::ComPtr<IDXGIFactory2> const& factory
	) noexcept {
		Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
		if (FAILED(factory.As(&factory5))) {
			return false;
		}
		BOOL allow_tearing = FALSE;
		HRESULT result = factory5->CheckFeatureSupport(
			DXGI_FEATURE_PRESENT_ALLOW_TEARING,
			&allow_tearing,
			sizeof(allow_tearing)
		);
		return SUCCEEDED(result) && allow_tearing;
	}

	Backend::LogicalDevice::PresentationEntry CreatePresentationEntry(
		Microsoft::WRL::ComPtr<IDXGIAdapter1> const& adapter,
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> const& queue,
		Backend::PresentationTarget target,
		D3D12_RESOURCE_DESC const& resource_descriptor,
		std::uint32_t frames_in_flight
	) {
		if (!target) {
			throw std::invalid_argument("D3D12 presentation target is null");
		}
		if (resource_descriptor.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER ||
			resource_descriptor.DepthOrArraySize != 1u ||
			resource_descriptor.MipLevels != 1u ||
			resource_descriptor.SampleDesc.Count != 1u ||
			resource_descriptor.SampleDesc.Quality != 0u ||
			resource_descriptor.Width > (std::numeric_limits<std::uint32_t>::max)()) {
			throw std::invalid_argument("D3D12 presentation source must be a single-sampled 2D texture");
		}

		Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
		ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(&factory)));
		bool allow_tearing = IsTearingSupported(factory);
		DXGI_SWAP_CHAIN_DESC1 descriptor{
			.Width = static_cast<UINT>(resource_descriptor.Width),
			.Height = resource_descriptor.Height,
			.Format = resource_descriptor.Format,
			.Stereo = FALSE,
			.SampleDesc = { 1u, 0u },
			.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
			.BufferCount = frames_in_flight,
			.Scaling = DXGI_SCALING_STRETCH,
			.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
			.AlphaMode = DXGI_ALPHA_MODE_IGNORE,
			.Flags = allow_tearing ?
				static_cast<UINT>(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) : 0u
		};
		Microsoft::WRL::ComPtr<IDXGISwapChain1> swapchain;
		ThrowIfFailed(factory->CreateSwapChainForHwnd(
			queue.Get(),
			target,
			&descriptor,
			nullptr,
			nullptr,
			&swapchain
		));
		ThrowIfFailed(factory->MakeWindowAssociation(target, DXGI_MWA_NO_ALT_ENTER));

		Backend::LogicalDevice::PresentationEntry result{
			.queue = queue,
			.format = descriptor.Format,
			.width = descriptor.Width,
			.height = descriptor.Height
		};
		ThrowIfFailed(swapchain.As(&result.swapchain));
		result.frames.reserve(descriptor.BufferCount);
		for (UINT index = 0u; index < descriptor.BufferCount; ++index) {
			Backend::LogicalDevice::PresentationEntry::FrameSlot frame;
			ThrowIfFailed(result.swapchain->GetBuffer(
				index,
				IID_PPV_ARGS(&frame.back_buffer)
			));
			result.frames.emplace_back(std::move(frame));
		}
		return result;
	}

	void ResizePresentationEntry(
		Backend::LogicalDevice::PresentationEntry& entry,
		Backend::Scheduler::QueueState const& queue,
		D3D12_RESOURCE_DESC const& resource_descriptor,
		std::uint32_t frames_in_flight
	) {
		for (auto const& frame : entry.frames) {
			if (frame.fence_value == 0u) {
				continue;
			}
			WaitForFence(queue.fence.Get(), frame.fence_value);
		}

		DXGI_SWAP_CHAIN_DESC1 descriptor;
		ThrowIfFailed(entry.swapchain->GetDesc1(&descriptor));
		entry.frames.clear();
		ThrowIfFailed(entry.swapchain->ResizeBuffers(
			frames_in_flight,
			static_cast<UINT>(resource_descriptor.Width),
			resource_descriptor.Height,
			resource_descriptor.Format,
			descriptor.Flags
		));
		entry.frames.reserve(frames_in_flight);
		for (UINT index = 0u; index < frames_in_flight; ++index) {
			Backend::LogicalDevice::PresentationEntry::FrameSlot frame;
			ThrowIfFailed(entry.swapchain->GetBuffer(
				index,
				IID_PPV_ARGS(&frame.back_buffer)
			));
			entry.frames.emplace_back(std::move(frame));
		}
		entry.format = resource_descriptor.Format;
		entry.width = static_cast<std::uint32_t>(resource_descriptor.Width);
		entry.height = resource_descriptor.Height;
	}

	struct D3D12CommandRecorder {
		execution::NativeCommandGraphBindings<Backend> const* bindings;
		ID3D12GraphicsCommandList* commands;
		std::vector<D3D12_RESOURCE_STATES>* states;
		Backend::Scheduler::QueueState const* queue;
		std::vector<Backend::GraphExecution::Batch::PresentationRequest>* presentations;
		mutable ID3D12DescriptorHeap* resource_heap = nullptr;
		mutable ID3D12DescriptorHeap* sampler_heap = nullptr;
		mutable execution::BeginRenderingCommand const* rendering = nullptr;
		mutable Backend::Pipeline const* pipeline = nullptr;

		static UINT Subresource(Backend::View const& view) {
			if (view.mip_level_count != 1u || view.array_layer_count != 1u) {
				throw std::invalid_argument("D3D12 rendering attachments must select one subresource");
			}
			auto descriptor = view.Resource()->GetDesc();
			if (descriptor.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D &&
				view.base_array_layer != 0u) {
				throw std::invalid_argument("D3D12 3D texture attachments do not support array layers");
			}
			return view.base_mip_level +
				view.base_array_layer * static_cast<UINT>(descriptor.MipLevels);
		}

		static void ValidateRenderArea(
			Backend::View const& view,
			execution::BeginRenderingCommand const& command
		) {
			if (command.offset_x < 0 || command.offset_y < 0 ||
				command.width == 0u || command.height == 0u) {
				throw std::invalid_argument("D3D12 rendering area is invalid");
			}
			auto descriptor = view.Resource()->GetDesc();
			auto width = descriptor.Width >> view.base_mip_level;
			auto height = static_cast<std::uint64_t>(descriptor.Height) >> view.base_mip_level;
			if (width == 0u) width = 1u;
			if (height == 0u) height = 1u;
			if (static_cast<std::uint64_t>(command.offset_x) + command.width > width ||
				static_cast<std::uint64_t>(command.offset_y) + command.height > height) {
				throw std::out_of_range("D3D12 rendering area exceeds an attachment");
			}
		}

		void Transition(
			execution::GraphResourceID resource_id,
			D3D12_RESOURCE_STATES destination
		) const {
			auto& source = (*states)[resource_id.value];
			if (source == destination) return;
			auto resource = bindings->resources[resource_id.value].get().impl->GetResource();
			auto barrier = TransitionBarrier(resource, source, destination);
			commands->ResourceBarrier(1u, &barrier);
			source = destination;
		}

		void operator()(execution::BeginRenderingCommand const& command) const {
			if (rendering) throw std::logic_error("D3D12 rendering commands cannot be nested");
			rendering = &command;
			std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> colors;
			colors.reserve(command.colors.size());
			for (auto const& color : command.colors) {
				auto const& view = bindings->views[color.view.value].get();
				if (view.type != Backend::View::Type::RenderTarget ||
					view.allocation.Get() != bindings->resources[color.resource.value].get().impl.Get()) {
					throw std::invalid_argument("D3D12 color attachment view does not match its resource");
				}
				Subresource(view);
				ValidateRenderArea(view, command);
				auto handle = view.CPU();
				colors.emplace_back(handle);
				if (!color.load) {
					float clear[] = {
						color.clear_red,
						color.clear_green,
						color.clear_blue,
						color.clear_alpha
					};
					commands->ClearRenderTargetView(handle, clear, 0u, nullptr);
				}
			}
			D3D12_CPU_DESCRIPTOR_HANDLE depth{};
			D3D12_CPU_DESCRIPTOR_HANDLE* depth_pointer = nullptr;
			if (command.depth_stencil) {
				auto const& view = bindings->views[command.depth_stencil->view.value].get();
				if (view.type != Backend::View::Type::DepthStencil ||
					view.allocation.Get() != bindings->resources[command.depth_stencil->resource.value].get().impl.Get()) {
					throw std::invalid_argument("D3D12 depth attachment view does not match its resource");
				}
				Subresource(view);
				ValidateRenderArea(view, command);
				depth = view.CPU();
				depth_pointer = &depth;
				D3D12_CLEAR_FLAGS flags = static_cast<D3D12_CLEAR_FLAGS>(0u);
				if (!command.depth_stencil->load_depth) flags |= D3D12_CLEAR_FLAG_DEPTH;
				if (!command.depth_stencil->load_stencil) flags |= D3D12_CLEAR_FLAG_STENCIL;
				if (flags != 0u) {
					commands->ClearDepthStencilView(
						depth,
						flags,
						command.depth_stencil->clear_depth,
						static_cast<UINT8>(command.depth_stencil->clear_stencil),
						0u,
						nullptr
					);
				}
			}
			commands->OMSetRenderTargets(
				static_cast<UINT>(colors.size()),
				colors.data(),
				FALSE,
				depth_pointer
			);
			D3D12_VIEWPORT viewport{
				.TopLeftX = static_cast<float>(command.offset_x),
				.TopLeftY = static_cast<float>(command.offset_y),
				.Width = static_cast<float>(command.width),
				.Height = static_cast<float>(command.height),
				.MinDepth = 0.0f,
				.MaxDepth = 1.0f
			};
			commands->RSSetViewports(1u, &viewport);
			D3D12_RECT scissor{
				.left = command.offset_x,
				.top = command.offset_y,
				.right = command.offset_x + static_cast<LONG>(command.width),
				.bottom = command.offset_y + static_cast<LONG>(command.height)
			};
			commands->RSSetScissorRects(1u, &scissor);
		}

		void operator()(execution::EndRenderingCommand const&) const {
			if (!rendering) throw std::logic_error("D3D12 EndRendering has no matching BeginRendering");
			for (auto const& color : rendering->colors) {
				auto const& source = bindings->views[color.view.value].get();
				if (color.resolve_view || color.resolve_resource) {
					if (!color.resolve_view || !color.resolve_resource) {
						throw std::invalid_argument("D3D12 resolve requires both a resource and a view");
					}
					auto const& destination = bindings->views[color.resolve_view->value].get();
					if (destination.allocation.Get() !=
						bindings->resources[color.resolve_resource->value].get().impl.Get() ||
						destination.format != source.format ||
						destination.type != Backend::View::Type::RenderTarget) {
						throw std::invalid_argument("D3D12 resolve attachment does not match its resource or format");
					}
					auto source_descriptor = source.Resource()->GetDesc();
					auto destination_descriptor = destination.Resource()->GetDesc();
					if (source_descriptor.SampleDesc.Count <= 1u ||
						destination_descriptor.SampleDesc.Count != 1u) {
						throw std::invalid_argument("D3D12 resolve requires multisampled source and single-sampled destination");
					}
					Transition(color.resource, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
					Transition(*color.resolve_resource, D3D12_RESOURCE_STATE_RESOLVE_DEST);
					commands->ResolveSubresource(
						destination.Resource(),
						Subresource(destination),
						source.Resource(),
						Subresource(source),
						source.format
					);
				}
				if (!color.store) {
					Transition(color.resource, D3D12_RESOURCE_STATE_RENDER_TARGET);
					auto subresource = Subresource(source);
					D3D12_DISCARD_REGION region{
						.NumRects = 0u,
						.pRects = nullptr,
						.FirstSubresource = subresource,
						.NumSubresources = 1u
					};
					commands->DiscardResource(source.Resource(), &region);
				}
			}
			if (rendering->depth_stencil &&
				!rendering->depth_stencil->store_depth &&
				!rendering->depth_stencil->store_stencil) {
				auto const& depth = bindings->views[rendering->depth_stencil->view.value].get();
				auto subresource = Subresource(depth);
				D3D12_DISCARD_REGION region{
					.NumRects = 0u,
					.pRects = nullptr,
					.FirstSubresource = subresource,
					.NumSubresources = 1u
				};
				commands->DiscardResource(depth.Resource(), &region);
			}
			rendering = nullptr;
		}

		void operator()(execution::BindPipelineCommand const& command) const {
			pipeline = &bindings->pipelines[command.pipeline.value].get();
			if (rendering && pipeline->compute) {
				throw std::invalid_argument("D3D12 compute pipelines cannot be bound in a rendering scope");
			}
			commands->SetPipelineState(pipeline->impl.Get());
			if (pipeline->compute) {
				commands->SetComputeRootSignature(pipeline->root_signature.Get());
			}
			else {
				commands->SetGraphicsRootSignature(pipeline->root_signature.Get());
				commands->IASetPrimitiveTopology(pipeline->primitive_topology);
			}
		}

		void operator()(execution::BindResourceGroupCommand const& command) const {
			auto const& group = bindings->resource_groups[command.group.value].get();
			if (!pipeline) {
				throw std::logic_error("D3D12 resource group requires a bound pipeline");
			}
			if (group.root_signature.Get() != pipeline->root_signature.Get()) {
				throw std::invalid_argument("D3D12 resource group was created for a different root signature");
			}
			if (group.space != command.index) {
				throw std::invalid_argument("D3D12 resource group index does not match its pipeline space");
			}
			if (resource_heap != group.resource_heap.Native() ||
				sampler_heap != group.sampler_heap.Native()) {
				ID3D12DescriptorHeap* heaps[] = {
					group.resource_heap.Native(),
					group.sampler_heap.Native()
				};
				commands->SetDescriptorHeaps(2u, heaps);
				resource_heap = group.resource_heap.Native();
				sampler_heap = group.sampler_heap.Native();
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

		void operator()(execution::BindVertexBufferCommand const& command) const {
			auto resource = bindings->resources[command.resource.value].get().impl->GetResource();
			auto descriptor = resource->GetDesc();
			D3D12_VERTEX_BUFFER_VIEW view{
				.BufferLocation = resource->GetGPUVirtualAddress() + command.offset,
				.SizeInBytes = static_cast<UINT>(descriptor.Width - command.offset),
				.StrideInBytes = command.stride
			};
			commands->IASetVertexBuffers(command.slot, 1u, &view);
		}

		void operator()(execution::BindIndexBufferCommand const& command) const {
			auto resource = bindings->resources[command.resource.value].get().impl->GetResource();
			auto descriptor = resource->GetDesc();
			D3D12_INDEX_BUFFER_VIEW view{
				.BufferLocation = resource->GetGPUVirtualAddress() + command.offset,
				.SizeInBytes = static_cast<UINT>(descriptor.Width - command.offset),
				.Format = command.uint32 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT
			};
			commands->IASetIndexBuffer(&view);
		}

		void operator()(execution::SetViewportCommand const& command) const noexcept {
			D3D12_VIEWPORT viewport{
				.TopLeftX = command.x,
				.TopLeftY = command.y,
				.Width = command.width,
				.Height = command.height,
				.MinDepth = command.minimum_depth,
				.MaxDepth = command.maximum_depth
			};
			commands->RSSetViewports(1u, &viewport);
		}

		void operator()(execution::SetScissorCommand const& command) const noexcept {
			D3D12_RECT scissor{
				.left = command.x,
				.top = command.y,
				.right = command.x + static_cast<LONG>(command.width),
				.bottom = command.y + static_cast<LONG>(command.height)
			};
			commands->RSSetScissorRects(1u, &scissor);
		}

		void operator()(execution::DrawCommand const& command) const noexcept {
			commands->DrawInstanced(
				command.vertex_count,
				command.instance_count,
				command.first_vertex,
				command.first_instance
			);
		}

		void operator()(execution::DrawIndexedCommand const& command) const noexcept {
			commands->DrawIndexedInstanced(
				command.index_count,
				command.instance_count,
				command.first_index,
				command.vertex_offset,
				command.first_instance
			);
		}

		void operator()(execution::DispatchCommand const& command) const {
			if (!pipeline || !pipeline->compute) {
				throw std::logic_error("D3D12 Dispatch requires a bound compute pipeline");
			}
			if (rendering) {
				throw std::logic_error("D3D12 Dispatch cannot execute in a rendering scope");
			}
			if (command.group_count_x == 0u || command.group_count_y == 0u || command.group_count_z == 0u) {
				throw std::invalid_argument("D3D12 Dispatch group counts must be non-zero");
			}
			commands->Dispatch(command.group_count_x, command.group_count_y, command.group_count_z);
		}

		void operator()(execution::CopyBufferCommand const& command) const noexcept {
			auto source = bindings->resources[command.source.value].get().impl->GetResource();
			auto destination = bindings->resources[command.destination.value].get().impl->GetResource();
			commands->CopyBufferRegion(
				destination,
				command.destination_offset,
				source,
				command.source_offset,
				command.size
			);
		}

		void operator()(execution::CopyBufferToTextureCommand const& command) const {
			auto source = bindings->resources[command.source.value].get().impl->GetResource();
			auto destination = bindings->resources[command.destination.value].get().impl->GetResource();
			auto descriptor = destination->GetDesc();
			auto const& region = command.destination_region;
			auto layers = descriptor.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
				? 1u : region.array_layer_count;
			auto block_height = D3D12TextureBlockHeight(descriptor.Format);
			auto rows_per_image = (command.source_layout.rows_per_image + block_height - 1u) /
				block_height;
			auto layer_stride = static_cast<std::size_t>(command.source_layout.bytes_per_row) *
				rows_per_image;
			if (command.source_layout.offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT != 0u ||
				command.source_layout.bytes_per_row % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT != 0u ||
				(layers > 1u && layer_stride % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT != 0u)) {
				throw std::invalid_argument("D3D12 buffer-to-texture layout is not aligned");
			}
			for (std::uint32_t layer = 0u; layer < layers; ++layer) {
				D3D12_TEXTURE_COPY_LOCATION source_location{
					.pResource = source,
					.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
					.PlacedFootprint = {
						.Offset = command.source_layout.offset + static_cast<std::size_t>(layer) *
							layer_stride,
						.Footprint = {
							.Format = descriptor.Format,
							.Width = region.width,
							.Height = region.height,
							.Depth = region.depth,
							.RowPitch = command.source_layout.bytes_per_row
						}
					}
				};
				D3D12_TEXTURE_COPY_LOCATION destination_location{
					.pResource = destination,
					.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
					.SubresourceIndex = region.mip_level +
						(region.base_array_layer + layer) * descriptor.MipLevels
				};
				commands->CopyTextureRegion(&destination_location, region.offset_x, region.offset_y,
					region.offset_z, &source_location, nullptr);
			}
		}

		void operator()(execution::CopyTextureToBufferCommand const& command) const {
			auto source = bindings->resources[command.source.value].get().impl->GetResource();
			auto destination = bindings->resources[command.destination.value].get().impl->GetResource();
			auto descriptor = source->GetDesc();
			auto const& region = command.source_region;
			auto layers = descriptor.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
				? 1u : region.array_layer_count;
			auto block_height = D3D12TextureBlockHeight(descriptor.Format);
			auto rows_per_image = (command.destination_layout.rows_per_image + block_height - 1u) /
				block_height;
			auto layer_stride = static_cast<std::size_t>(command.destination_layout.bytes_per_row) *
				rows_per_image;
			if (command.destination_layout.offset % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT != 0u ||
				command.destination_layout.bytes_per_row % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT != 0u ||
				(layers > 1u && layer_stride % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT != 0u)) {
				throw std::invalid_argument("D3D12 texture-to-buffer layout is not aligned");
			}
			for (std::uint32_t layer = 0u; layer < layers; ++layer) {
				D3D12_TEXTURE_COPY_LOCATION source_location{
					.pResource = source,
					.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
					.SubresourceIndex = region.mip_level +
						(region.base_array_layer + layer) * descriptor.MipLevels
				};
				D3D12_TEXTURE_COPY_LOCATION destination_location{
					.pResource = destination,
					.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
					.PlacedFootprint = {
						.Offset = command.destination_layout.offset + static_cast<std::size_t>(layer) *
							layer_stride,
						.Footprint = { descriptor.Format, region.width, region.height, region.depth,
							command.destination_layout.bytes_per_row }
					}
				};
				D3D12_BOX box{ region.offset_x, region.offset_y, region.offset_z,
					region.offset_x + region.width, region.offset_y + region.height,
					region.offset_z + region.depth };
				commands->CopyTextureRegion(&destination_location, 0u, 0u, 0u, &source_location, &box);
			}
		}

		void operator()(execution::CopyTextureCommand const& command) const {
			auto source = bindings->resources[command.source.value].get().impl->GetResource();
			auto destination = bindings->resources[command.destination.value].get().impl->GetResource();
			auto source_descriptor = source->GetDesc();
			auto destination_descriptor = destination->GetDesc();
			auto const& source_region = command.source_region;
			auto const& destination_region = command.destination_region;
			auto layers = source_descriptor.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D
				? 1u : source_region.array_layer_count;
			for (std::uint32_t layer = 0u; layer < layers; ++layer) {
				D3D12_TEXTURE_COPY_LOCATION source_location{ source,
					D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
					{ source_region.mip_level + (source_region.base_array_layer + layer) *
						source_descriptor.MipLevels } };
				D3D12_TEXTURE_COPY_LOCATION destination_location{ destination,
					D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
					{ destination_region.mip_level + (destination_region.base_array_layer + layer) *
						destination_descriptor.MipLevels } };
				D3D12_BOX box{ source_region.offset_x, source_region.offset_y, source_region.offset_z,
					source_region.offset_x + source_region.width,
					source_region.offset_y + source_region.height,
					source_region.offset_z + source_region.depth };
				commands->CopyTextureRegion(&destination_location, destination_region.offset_x,
					destination_region.offset_y, destination_region.offset_z, &source_location, &box);
			}
		}

		void operator()(execution::PresentCommand const& command) const {
			if (rendering) {
				throw std::logic_error("D3D12 Present cannot execute in a rendering scope");
			}
			if (queue->type != D3D12_COMMAND_LIST_TYPE_DIRECT) {
				throw std::invalid_argument("D3D12 Present requires a graphics queue");
			}
			presentations->emplace_back(
				bindings->presentation_targets[command.target.value],
				command.source,
				bindings->resources[command.source.value].get().impl->GetResource(),
				D3D12_RESOURCE_STATE_COMMON,
				command.vertical_sync,
				command.frames_in_flight
			);
		}
	};

	struct D3D12CompletionPoll {
		std::vector<std::pair<std::shared_ptr<Backend::Scheduler::QueueState>, std::uint64_t>> fences;
		std::shared_ptr<std::atomic<HRESULT>> device_error;

		bool operator()() const noexcept {
			for (auto const& fence : fences) {
				auto completed = fence.first->fence->GetCompletedValue();
				if (completed == (std::numeric_limits<std::uint64_t>::max)()) {
					auto reason = fence.first->device->GetDeviceRemovedReason();
					device_error->store(
						FAILED(reason) ? reason : DXGI_ERROR_DEVICE_REMOVED,
						std::memory_order_release
					);
					return true;
				}
				if (completed < fence.second) {
					return false;
				}
			}
			return true;
		}
	};

	struct D3D12GraphCompletion {
		execution::GraphCompletion completion;
		std::shared_ptr<std::atomic<HRESULT>> device_error;
		std::exception_ptr submission_error;

		void operator()() const noexcept {
			auto result = device_error->load(std::memory_order_acquire);
			if (FAILED(result)) {
				try {
					ThrowIfFailed(result);
				}
				catch (...) {
					auto error = std::current_exception();
					completion.SetError(completion.operation, error);
				}
				return;
			}
			if (submission_error) {
				completion.SetError(completion.operation, submission_error);
				return;
			}
			completion.SetValue(completion.operation);
		}
	};

	void AddSubmittedQueue(
		std::vector<std::shared_ptr<Backend::Scheduler::QueueState>>& queues,
		std::shared_ptr<Backend::Scheduler::QueueState> const& queue
	) {
		if (std::ranges::find(queues, queue) == queues.end()) {
			queues.push_back(queue);
		}
	}

	void MarkPresentationFramesSubmitted(
		Backend::GraphExecution::Batch& batch
	) {
		for (auto& presentation : batch.in_flight_presentations) {
			auto& entry = presentation.entry.Get();
			std::unique_lock<std::mutex> lock(*entry.mutex);
			entry.frames[presentation.frame_index].fence_value = batch.fence_value;
		}
	}

}
namespace fyuu_rhi::d3d12 {
	Backend::ExecutableGraph Backend::CompileCommandGraph(Backend::CommandGraph const& graph) {
		return execution::MakeExecutableGraph<Backend>(graph);
	}

	std::shared_ptr<Backend::Scheduler::QueueState> const&
	Backend::Scheduler::QueueCollection::Select(execution::GraphNodeFlagBits capability) const {
		using Flag = execution::GraphNodeFlagBits;
		if ((capability & (Flag::Graphics | Flag::Present)) != Flag::None && graphics) {
			return graphics;
		}
		if ((capability & Flag::Compute) != Flag::None && compute) {
			return compute;
		}
		if ((capability & Flag::Copy) != Flag::None && copy) {
			return copy;
		}
		throw std::invalid_argument("Command graph batch requires an unavailable D3D12 queue");
	}

	Backend::GraphExecution CreateGraphExecution(
		Backend::Scheduler const& scheduler,
		Backend::ExecutableGraph const& graph
	) {
		Backend::GraphExecution result{ scheduler, graph };
		std::vector<D3D12_RESOURCE_STATES> stable_states;
		stable_states.reserve(graph->impl->bindings.resources.size());
		for (auto const& resource : graph->impl->bindings.resources) {
			stable_states.push_back(resource.get().stable_state);
		}
		auto states = stable_states;
		auto const& last_users = graph->plan.last_resource_users;
		result.batches.reserve(graph->plan.batches.size());
		for (auto const& batch : graph->plan.batches) {
			auto const& queue = scheduler.impl->queues.Select(batch.queue_flags);
			auto CreateCommands = [&queue]() {
				Backend::Scheduler::CommandEntry entry;
				ThrowIfFailed(queue->device->CreateCommandAllocator(
					queue->type,
					IID_PPV_ARGS(&entry.allocator)
				));
				ThrowIfFailed(queue->device->CreateCommandList(
					0u,
					queue->type,
					entry.allocator.Get(),
					nullptr,
					IID_PPV_ARGS(&entry.impl)
				));
				return entry;
			};
			auto commands = queue->command_pool->Acquire(
				CreateCommands,
				ResetCommandEntry
			);
			auto* command_list = commands.Get().impl.Get();
			std::vector<Backend::GraphExecution::Batch::PresentationRequest> presentations;
			D3D12CommandRecorder recorder{
				&graph->impl->bindings,
				command_list,
				&states,
				queue.get(),
				&presentations
			};
			for (auto node_id : batch.nodes) {
				auto const& node = graph->impl->descriptor.nodes[node_id.value];
				for (auto const& access : node.accesses) {
					auto desired = D3D12ResourceState(access.flags);
					auto resource = graph->impl->bindings.resources[access.resource.value]
						.get().impl->GetResource();
					if (UsesCopyQueueCommonLayout(
						queue->type,
						states[access.resource.value],
						desired
					)) {
						continue;
					}
					if (!ResourceStateContains(states[access.resource.value], desired)) {
						auto barrier = TransitionBarrier(
							resource,
							states[access.resource.value],
							desired
						);
						command_list->ResourceBarrier(1u, &barrier);
						states[access.resource.value] = desired;
					}
					else if (desired == D3D12_RESOURCE_STATE_UNORDERED_ACCESS &&
						(access.flags & execution::GraphAccessFlagBits::Write) !=
							execution::GraphAccessFlagBits::None) {
						auto barrier = UnorderedAccessBarrier(resource);
						command_list->ResourceBarrier(1u, &barrier);
					}
				}
				for (auto const& command : node.commands) {
					std::visit(recorder, command);
				}
				for (auto const& release : batch.release_barriers) {
					if (release.source.value != node_id.value ||
						scheduler.impl->queues.Select(release.source_queue) ==
							scheduler.impl->queues.Select(release.destination_queue) ||
						states[release.resource.value] == D3D12_RESOURCE_STATE_COMMON) {
						continue;
					}
					auto resource = graph->impl->bindings.resources[release.resource.value]
						.get().impl->GetResource();
					auto barrier = TransitionBarrier(
						resource,
						states[release.resource.value],
						D3D12_RESOURCE_STATE_COMMON
					);
					command_list->ResourceBarrier(1u, &barrier);
					states[release.resource.value] = D3D12_RESOURCE_STATE_COMMON;
				}
				for (auto const& access : node.accesses) {
					if (last_users[access.resource.value].value != node_id.value ||
						states[access.resource.value] == stable_states[access.resource.value]) {
						continue;
					}
					auto resource = graph->impl->bindings.resources[access.resource.value]
						.get().impl->GetResource();
					auto barrier = TransitionBarrier(
						resource,
						states[access.resource.value],
						stable_states[access.resource.value]
					);
					command_list->ResourceBarrier(1u, &barrier);
					states[access.resource.value] = stable_states[access.resource.value];
				}
			}
			if (recorder.rendering) {
				throw std::logic_error("D3D12 rendering scope must end in the batch where it begins");
			}
			ThrowIfFailed(command_list->Close());
			commands.Get().closed = true;
			for (auto& presentation : presentations) {
				presentation.source_state = states[presentation.source_id.value];
			}
			result.batches.emplace_back(queue, std::move(commands), 0u);
			result.batches.back().presentation_requests = std::move(presentations);
		}
		return result;
	}

	void StartGraphExecution(
		Backend::GraphExecution& graph_execution,
		execution::GraphCompletion const& completion
	) {
		auto const& plans = graph_execution.graph->plan.batches;
		auto device_error = std::make_shared<std::atomic<HRESULT>>(S_OK);
		D3D12CompletionPoll poll{ .device_error = device_error };
		D3D12GraphCompletion graph_completion{
			.completion = completion,
			.device_error = device_error
		};
		std::vector<std::shared_ptr<Backend::Scheduler::QueueState>> submitted_queues;
		try {
		for (std::size_t index = 0u; index < plans.size(); ++index) {
			auto& batch = graph_execution.batches[index];
			std::unique_lock<std::mutex> lock(batch.queue->submission_mutex);
			for (auto dependency : plans[index].dependencies) {
				auto const& source = graph_execution.batches[dependency];
				if (source.queue != batch.queue) {
					ThrowIfFailed(batch.queue->impl->Wait(
						source.queue->fence.Get(),
						source.fence_value
					));
				}
			}
			ID3D12CommandList* commands[] = { batch.commands.Get().impl.Get() };
			batch.queue->impl->ExecuteCommandLists(1u, commands);
			AddSubmittedQueue(submitted_queues, batch.queue);
			for (auto const& request : batch.presentation_requests) {
				auto descriptor = request.source->GetDesc();
				auto presentation = graph_execution.scheduler.impl->presentation_cache->Acquire(
					request.target,
					CreatePresentationEntry,
					graph_execution.scheduler.impl->physical_device,
					batch.queue->impl,
					request.target,
					descriptor,
					request.frames_in_flight
				);
				auto& entry = presentation.Get();
				if (entry.queue.Get() != batch.queue->impl.Get() ||
					entry.format != descriptor.Format ||
					entry.width != descriptor.Width ||
					entry.height != descriptor.Height ||
					entry.frames.size() != request.frames_in_flight) {
					if (entry.queue.Get() != batch.queue->impl.Get()) {
						throw std::invalid_argument(
							"D3D12 presentation target cannot migrate between queues"
						);
					}
					std::unique_lock<std::mutex> presentation_lock(*entry.mutex);
					ResizePresentationEntry(
						entry,
						*batch.queue,
						descriptor,
						request.frames_in_flight
					);
				}

				{
					auto& presentation_entry = presentation.Get();
					std::unique_lock<std::mutex> presentation_lock(*presentation_entry.mutex);
					auto frame_index = presentation_entry.swapchain->GetCurrentBackBufferIndex();
					if (frame_index >= presentation_entry.frames.size()) {
						throw std::out_of_range("D3D12 swapchain returned an invalid back buffer index");
					}
					auto& frame = presentation_entry.frames[frame_index];
					if (frame.fence_value != 0u) {
						ThrowIfFailed(batch.queue->impl->Wait(
							batch.queue->fence.Get(),
							frame.fence_value
						));
					}
					auto CreateCommands = [&batch]() {
						Backend::Scheduler::CommandEntry entry;
						ThrowIfFailed(batch.queue->device->CreateCommandAllocator(
							batch.queue->type,
							IID_PPV_ARGS(&entry.allocator)
						));
						ThrowIfFailed(batch.queue->device->CreateCommandList(
							0u,
							batch.queue->type,
							entry.allocator.Get(),
							nullptr,
							IID_PPV_ARGS(&entry.impl)
						));
						return entry;
					};
					auto present_commands = batch.queue->command_pool->Acquire(
						CreateCommands,
						ResetCommandEntry
					);
					auto command_list = present_commands.Get().impl.Get();
					bool transition_source = !ResourceStateContains(
						request.source_state,
						D3D12_RESOURCE_STATE_COPY_SOURCE
					);
					if (transition_source) {
						auto source = TransitionBarrier(
							request.source.Get(),
							request.source_state,
							D3D12_RESOURCE_STATE_COPY_SOURCE
						);
						command_list->ResourceBarrier(1u, &source);
					}
					auto destination = TransitionBarrier(
						frame.back_buffer.Get(),
						D3D12_RESOURCE_STATE_PRESENT,
						D3D12_RESOURCE_STATE_COPY_DEST
					);
					command_list->ResourceBarrier(1u, &destination);
					command_list->CopyResource(frame.back_buffer.Get(), request.source.Get());
					auto present = TransitionBarrier(
						frame.back_buffer.Get(),
						D3D12_RESOURCE_STATE_COPY_DEST,
						D3D12_RESOURCE_STATE_PRESENT
					);
					command_list->ResourceBarrier(1u, &present);
					if (transition_source) {
						auto source = TransitionBarrier(
							request.source.Get(),
							D3D12_RESOURCE_STATE_COPY_SOURCE,
							request.source_state
						);
						command_list->ResourceBarrier(1u, &source);
					}
					ThrowIfFailed(command_list->Close());
					present_commands.Get().closed = true;
					ID3D12CommandList* presentation_commands[] = { command_list };
					batch.queue->impl->ExecuteCommandLists(1u, presentation_commands);
					DXGI_SWAP_CHAIN_DESC1 swapchain_descriptor;
					ThrowIfFailed(presentation_entry.swapchain->GetDesc1(&swapchain_descriptor));
					bool allow_tearing =
						(swapchain_descriptor.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) != 0u;
					auto synchronization_interval = request.vertical_sync ? 1u : 0u;
					UINT presentation_flags =
						!request.vertical_sync && allow_tearing ?
						static_cast<UINT>(DXGI_PRESENT_ALLOW_TEARING) : 0u;
					ThrowIfFailed(
						presentation_entry.swapchain->Present(
							synchronization_interval,
							presentation_flags
						)
					);
					batch.in_flight_presentations.emplace_back(
						std::move(presentation),
						std::move(present_commands),
						frame_index
					);
				}
			}
			batch.fence_value = batch.queue->next_fence_value.fetch_add(
				1u,
				std::memory_order::relaxed
			);
			ThrowIfFailed(batch.queue->impl->Signal(
				batch.queue->fence.Get(),
				batch.fence_value
			));
			MarkPresentationFramesSubmitted(batch);
		}
		for (auto const& batch : graph_execution.batches) {
			poll.fences.emplace_back(batch.queue, batch.fence_value);
		}
		}
		catch (...) {
			graph_completion.submission_error = std::current_exception();
			for (auto const& queue : submitted_queues) {
				std::unique_lock<std::mutex> lock(queue->submission_mutex);
				auto terminal_value = queue->next_fence_value.fetch_add(
					1u,
					std::memory_order::relaxed
				);
				auto result = queue->impl->Signal(queue->fence.Get(), terminal_value);
				if (SUCCEEDED(result)) {
					poll.fences.emplace_back(queue, terminal_value);
					continue;
				}
				auto reason = queue->device->GetDeviceRemovedReason();
				device_error->store(
					FAILED(reason) ? reason : result,
					std::memory_order_release
				);
			}
		}
		if (poll.fences.empty()) {
			graph_completion();
			return;
		}
		graph_execution.scheduler.impl->completion_service->Enqueue(
			std::move(poll),
			std::move(graph_completion)
		);
	}

	void StartSchedulerExecution(
		Backend::Scheduler const& scheduler,
		execution::SchedulerCompletion const& completion
	) {
		auto queue = scheduler.impl->queues.graphics;
		if (!queue) queue = scheduler.impl->queues.compute;
		if (!queue) queue = scheduler.impl->queues.copy;
		if (!queue) {
			throw std::logic_error("D3D12 scheduler has no execution queue");
		}

		auto device_error = std::make_shared<std::atomic<HRESULT>>(S_OK);
		std::uint64_t value;
		{
			std::unique_lock<std::mutex> lock(queue->submission_mutex);
			value = queue->next_fence_value.fetch_add(1u, std::memory_order::relaxed);
			auto result = queue->impl->Signal(queue->fence.Get(), value);
			if (FAILED(result)) {
				auto reason = queue->device->GetDeviceRemovedReason();
				ThrowIfFailed(FAILED(reason) ? reason : result);
			}
		}
		D3D12CompletionPoll poll{
			.fences = { { queue, value } },
			.device_error = device_error
		};
		D3D12GraphCompletion schedule_completion{
			.completion = completion,
			.device_error = device_error
		};
		scheduler.impl->completion_service->Enqueue(
			std::move(poll),
			std::move(schedule_completion)
		);
	}

	void StartDeferredDestroy(
		Backend::Scheduler const&,
		execution::DeferredDestroy const& deferred_destroy
	) {
		deferred_destroy.Destroy(deferred_destroy.object);
		deferred_destroy.completion.SetValue(deferred_destroy.completion.operation);
	}

	void StartMapResource(
		Backend::Scheduler const&,
		Backend::Resource& resource,
		execution::ResourceMapRequest const& request
	) {
		D3D12_RANGE read_range{
			request.read ? request.offset : 0u,
			request.read ? request.offset + request.size : 0u
		};
		void* mapped = nullptr;
		ThrowIfFailed(resource.impl->GetResource()->Map(0u, &read_range, &mapped));
		request.completion.SetValue(
			request.completion.operation,
			static_cast<std::byte*>(mapped) + request.offset
		);
	}

	void StartUnmapResource(
		Backend::Scheduler const&,
		Backend::Resource& resource,
		execution::ResourceUnmapRequest const& request
	) {
		D3D12_RANGE written_range{
			request.write ? request.offset : 0u,
			request.write ? request.offset + request.size : 0u
		};
		resource.impl->GetResource()->Unmap(0u, &written_range);
		request.completion.SetValue(request.completion.operation);
	}

}
#endif // defined(_WIN32)
