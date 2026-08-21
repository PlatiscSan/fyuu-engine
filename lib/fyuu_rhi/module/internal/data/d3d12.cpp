module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <cstdint>

#include <optional>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <D3D12MemAlloc.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#endif // defined(_WIN32)

module fyuu_rhi:d3d12_data;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core;
import :execution;
import :pipeline;
import :d3d12_descriptor_allocator;
import :d3d12_device_removal_tracker;
import plastic.static_hash_table;
import plastic.static_list;
import plastic.lru;

namespace fyuu_rhi::d3d12 {

	struct Instance {
		Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
	};

	struct PhysicalDevice {
		Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
	};

	struct LogicalDevice {
		Microsoft::WRL::ComPtr<ID3D12Device> impl;
		DeviceRemovalTracker rm_tracker;
		Microsoft::WRL::ComPtr<D3D12MA::Allocator> memory_allocator;
		DescriptorAllocator resource_descriptors;
		DescriptorAllocator render_target_descriptors;
		DescriptorAllocator depth_stencil_descriptors;
		DescriptorAllocator sampler_descriptors;
		Microsoft::WRL::ComPtr<ID3D12CommandSignature> multidraw;
		Microsoft::WRL::ComPtr<ID3D12CommandSignature> multidraw_indexed;
		Microsoft::WRL::ComPtr<ID3D12CommandSignature> dispatch_indirect;
	};

	struct PresentationContext {
		struct BackBuffer {
			Microsoft::WRL::ComPtr<ID3D12Resource> resource;
			std::uint64_t fence_value;
		};

		struct SwapChain {
			std::vector<BackBuffer> back_buffers;
			Microsoft::WRL::ComPtr<IDXGISwapChain3> impl;
			bool tearing_supported = false;
		};

		using List = plastic::ds::StaticList<
			std::pair<HWND const, SwapChain>,
			32u
		>;
		using HashTable = plastic::ds::StaticHashTable<
			HWND const,
			List::iterator,
			32u,
			std::hash<HWND>
		>;

		plastic::ds::LRUCache<HashTable, List, 32u> cache;
		std::mutex mutex;
	};

	struct QueueContext final : std::enable_shared_from_this<QueueContext> {
		using PresentationContext = d3d12::PresentationContext;

		struct ManagedCommandList {
			std::shared_ptr<QueueContext> owner;
			Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> impl;
			std::uint64_t fence_value = 0u;
			bool is_open = false;

			ManagedCommandList(
				std::shared_ptr<QueueContext> const& owner,
				Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>&& impl,
				std::uint64_t fence_value,
				bool is_open
			) noexcept
				: owner(owner),
				impl(std::move(impl)),
				fence_value(fence_value),
				is_open(is_open) {
			}

			ManagedCommandList(ManagedCommandList const&) = delete;
			ManagedCommandList& operator=(ManagedCommandList const&) = delete;
			ManagedCommandList(ManagedCommandList&& other) noexcept
				: owner(std::move(other.owner)),
				impl(std::move(other.impl)),
				fence_value(other.fence_value),
				is_open(other.is_open) {
				other.fence_value = 0u;
				other.is_open = false;
			}
			ManagedCommandList& operator=(ManagedCommandList&& other) noexcept;
			~ManagedCommandList() noexcept;
		};

		Microsoft::WRL::ComPtr<ID3D12CommandQueue> impl;
		Microsoft::WRL::ComPtr<ID3D12Fence> fence;
		std::uint64_t next_fence_value = 1u;
		std::unique_ptr<PresentationContext> presentation_context;
		std::deque<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>> command_lists;
		std::mutex command_lists_mutex;
		std::mutex submission_mutex;
	};

	struct CompletionToken {
		std::vector<QueueContext::ManagedCommandList> command_lists;
		std::exception_ptr exception;
		bool is_cancelled;
	};

	struct CommandSchedulerContext {
		using Queues = std::unordered_map<
			execution::QueueType,
			std::shared_ptr<QueueContext>
		>;

		Queues queues;
	};

	struct Resource {
		DescriptorAllocator resource_descriptors;
		DescriptorAllocator render_target_descriptors;
		DescriptorAllocator depth_stencil_descriptors;
		Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation;
	};

	struct View {
		enum class Type : std::uint8_t {
			ShaderResource,
			UnorderedAccess,
			RenderTarget,
			DepthStencil
		};
		std::array<ManagedDescriptorHandle, 4> descriptors;
		std::uint32_t base_mip_level = 0u;
		std::uint32_t mip_level_count = 1u;
		std::uint32_t base_array_layer = 0u;
		std::uint32_t array_layer_count = 1u;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	};

	struct Sampler {
		ManagedDescriptorHandle descriptor;
	};

	struct Pipeline {
		DescriptorAllocator resource_descriptors;
		DescriptorAllocator sampler_descriptors;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
		Microsoft::WRL::ComPtr<ID3D12PipelineState> impl;
		std::vector<pipeline::BindingMetadata> bindings;
		D3D_PRIMITIVE_TOPOLOGY primitive_topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
		bool compute = false;
	};

	struct PipelineResourceGroup {
		struct Table {
			std::uint32_t root_parameter = 0u;
			ManagedDescriptorRange descriptors;
		};

		std::uint32_t space = 0u;
		Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
		ManagedDescriptorHeap resource_heap;
		ManagedDescriptorHeap sampler_heap;
		std::vector<Table> tables;
	};

} // namespace fyuu_rhi::d3d12::data
#endif // defined(_WIN32)
