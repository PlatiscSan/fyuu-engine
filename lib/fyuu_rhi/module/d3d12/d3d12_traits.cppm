/* the d3d12 pattern
module;
#include <version>
#if !defined(__cpp_lib_modules)

#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <dxgi1_3.h>
#include <d3d12.h>
#include <wrl.h>
#endif // defined(_WIN32)
export module fyuu_rhi:;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
namespace fyuu_rhi::d3d12 {

}
#endif // defined(_WIN32)

*/

module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>

#include <exception>
#include <stdexcept>

#include <memory>

#include <deque>
#include <vector>

#include <functional>

#include <cstdint>
#include <utility>

#include <atomic>
#include <mutex>
#include <thread>

#include <unordered_map>

#include <optional>
#include <variant>

#include <string_view>

#include <compare>
#include <span>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <dxgi1_3.h>
#include <D3D12MemAlloc.h>
#include <dxcapi.h>
#include <wrl.h>
#endif // defined(_WIN32)
export module fyuu_rhi:d3d12_traits;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core_types;
import :resource_types;
import :execution_types;
import :d3d12_utility;
import :d3d12_descriptor_allocator;
import :d3d12_device_removal_tracker;
import :sampler_types;
import :pipeline_types;
import :native_pipeline_binding;
import :execution_types;
import plastic.static_hash_table;
import plastic.static_list;
import plastic.lru;

namespace fyuu_rhi::d3d12 {

	using namespace fyuu_rhi::pipeline;
	export struct Backend {
		using PlatformHandle = HWND;
		using Instance = Microsoft::WRL::ComPtr<IDXGIFactory2>;
		using PhysicalDevice = Microsoft::WRL::ComPtr<IDXGIAdapter1>;

		struct LogicalDevice {
			Microsoft::WRL::ComPtr<ID3D12Device> impl;
			DeviceRemovalTracker rm_tracker;
			Microsoft::WRL::ComPtr<D3D12MA::Allocator> mem_alloc;
			DescriptorAllocator univ_alloc;
			DescriptorAllocator rtv_alloc;
			DescriptorAllocator dsv_alloc;
			DescriptorAllocator sampler_alloc;
			Microsoft::WRL::ComPtr<ID3D12CommandSignature> multidraw;
			Microsoft::WRL::ComPtr<ID3D12CommandSignature> multidraw_indexed;
			Microsoft::WRL::ComPtr<ID3D12CommandSignature> dispatch_indirect;
		};

		struct Resource {
			Microsoft::WRL::ComPtr<D3D12MA::Allocation> impl;
			D3D12_RESOURCE_STATES stable_state = D3D12_RESOURCE_STATE_COMMON;
		};

		struct View {
			enum class Type : std::uint8_t {
				ShaderResource,
				UnorderedAccess,
				RenderTarget,
				DepthStencil
			};

			std::unordered_map<Type, ManagedDescriptorHandle> impl;
			std::uint32_t base_mip_level = 0u;
			std::uint32_t mip_level_count = 1u;
			std::uint32_t base_array_layer = 0u;
			std::uint32_t array_layer_count = 1u;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

			[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CPU(Type type) const {
				return impl.at(type).CPU();
			}
		};

		using Sampler = ManagedDescriptorHandle;

		struct Pipeline {
			bool compute = false;
			Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature;
			Microsoft::WRL::ComPtr<ID3D12PipelineState> impl;
			D3D_PRIMITIVE_TOPOLOGY primitive_topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
			std::vector<PipelineBindingMetadata> bindings;
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

		struct QueueContext final 
			: public std::enable_shared_from_this<QueueContext> {
			/// Owns one command list from acquisition until it can return to the queue pool.
			/// The corresponding allocator is retained in the list's private data.
			struct ManagedCommandList {
				/// Keeps the native queue, fence, and pool alive while the list is outstanding.
				/// Resetting owner deliberately prevents an unsafe list from being recycled.
				std::shared_ptr<QueueContext> owner;
				/// Native list acquired from or eventually returned to owner->command_lists.
				Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> impl;
				/// Fence after which the list and allocator may reset; zero means unsubmitted.
				std::uint64_t fence_value = 0u;
				/// True between acquisition/Reset and a successful Close.
				bool is_open = false;

				ManagedCommandList() noexcept = default;

				ManagedCommandList(
					std::shared_ptr<QueueContext> const& owner_,
					Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> const& impl_,
					std::uint64_t fence_value_,
					bool is_open_
				) noexcept : owner(owner_),
					impl(impl_),
					fence_value(fence_value_),
					is_open(is_open_) {}

				ManagedCommandList(ManagedCommandList const&) = delete;
				ManagedCommandList& operator=(ManagedCommandList const&) = delete;
				ManagedCommandList(ManagedCommandList&&) noexcept = default;
				ManagedCommandList& operator=(ManagedCommandList&&) noexcept = default;
				~ManagedCommandList() noexcept;
			};

			struct PresentationContext {
				struct BackBuffer {
					/// Cache ownership keeps the DXGI back buffer alive across executions.
					Microsoft::WRL::ComPtr<ID3D12Resource> resource;
					/// Fence signaled after Present; zero means the frame has never been used.
					std::uint64_t fence_value;
				};

				struct SwapChain {
					/// Indexed by IDXGISwapChain3::GetCurrentBackBufferIndex().
					std::vector<BackBuffer> back_buffers;
					Microsoft::WRL::ComPtr<IDXGISwapChain3> impl;
					/// Captured at creation and used to select legal Present flags.
					bool tearing_supported = false;
				};

				using List = plastic::ds::StaticList<std::pair<HWND const, SwapChain>, 32u>;
				using HashTable = plastic::ds::StaticHashTable<HWND const, List::iterator, 32u, std::hash<HWND>>;

				plastic::ds::LRUCache<HashTable, List, 32u> cache;
			};

			Microsoft::WRL::ComPtr<ID3D12CommandQueue> impl;
			Microsoft::WRL::ComPtr<ID3D12Fence> fence;
			/// Protected by SchedulerContext::Implementation::execution_mutex.
			std::uint64_t next_fence_value = 1u;
			/// Graphics/direct queues own this; compute/copy queues leave it null.
			std::unique_ptr<PresentationContext> presentation_context;
			/// Closed command lists available for reuse.
			std::deque<Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>> command_lists;
			/// Protects only command_lists, never native queue submission.
			std::mutex command_lists_mutex;
		};

		/// Pollable ownership of all command lists in one execution transaction.
		struct CompletionToken {
			/// Retains queues, lists, and allocators until their fences complete.
			std::vector<QueueContext::ManagedCommandList> command_lists;
			/// Recording or submission error selected in deterministic batch order.
			std::exception_ptr exception;
			/// True only when stop was observed before phase 3.
			bool is_cancelled = false;

			CompletionToken() noexcept = default;

			CompletionToken(
				std::vector<QueueContext::ManagedCommandList>&& command_lists_,
				std::exception_ptr const& exception_,
				bool is_cancelled_
			) noexcept : command_lists(std::move(command_lists_)),
				exception(exception_),
				is_cancelled(is_cancelled_) {}

			CompletionToken(std::exception_ptr const& exception_) noexcept
				: command_lists(),
				exception(exception_),
				is_cancelled(false) {}

			CompletionToken(bool is_cancelled_) noexcept
				: command_lists(),
				exception(nullptr),
				is_cancelled(is_cancelled_) {}

			CompletionToken(CompletionToken const&) = delete;
			CompletionToken& operator=(CompletionToken const&) = delete;
			CompletionToken(CompletionToken&&) noexcept = default;
			CompletionToken& operator=(CompletionToken&&) noexcept = default;

			[[nodiscard]] bool Poll() noexcept;
			[[nodiscard]] std::exception_ptr Error() const noexcept;
			[[nodiscard]] bool IsStopped() const noexcept;
		};

		struct SchedulerContext {
			using Queues = std::unordered_map<execution::QueueType, std::shared_ptr<QueueContext>>;
			/// Shared as one allocation so queue state cannot be detached from its lock.
			struct Implementation {
				/// Logical roles may alias a QueueContext, notably Graphics and Present.
				Queues queues;
				/// Serializes whole ExecuteCommands transactions for this scheduler.
				/// Parallel recording and per-queue submission occur inside the transaction.
				std::mutex execution_mutex;
			};

			/// Null only for a default-constructed, uninitialized context.
			std::shared_ptr<Implementation> impl;
			std::strong_ordering operator<=>(SchedulerContext const& other) const noexcept = default;
		};

		static Microsoft::WRL::ComPtr<IDXGIFactory2> CreateInstance(std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver);

		static std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter1>> EnumeratePhysicalDevices(Microsoft::WRL::ComPtr<IDXGIFactory2> const& factory);

		static PhysicalDeviceInfo GetPhysicalDeviceInfo(Microsoft::WRL::ComPtr<IDXGIAdapter1> const& adapter);

		static LogicalDevice CreateLogicalDevice(Microsoft::WRL::ComPtr<IDXGIAdapter1> const& adapter);

		static Resource CreateBuffer(LogicalDevice const& ld, std::size_t size_in_bytes, ResourceFlags const& flags);

		static Resource CreateTexture(LogicalDevice const& ld, std::size_t width, std::size_t height, std::size_t depth_arr_layers, std::size_t mip_lvl_cnt, ResourceFlags const& flags);

		static View CreateTextureView(LogicalDevice& ld, Resource const& res, std::size_t base_mip_lvl, std::size_t mip_lvl_cnt, std::size_t base_arr_layer, std::size_t arr_layer_cnt, ResourceFlags const& flags);

		static View CreateBufferView(LogicalDevice& ld, Resource const& res, std::size_t offset, std::size_t range, ResourceFlags const& flags);

		static Sampler CreateSampler(LogicalDevice& ld, SamplerDescriptor const& descriptor);

		static Pipeline CreateGraphicsPipeline(LogicalDevice const& ld, GraphicsPipelineDescriptor const& desc);

		static Pipeline CreateComputePipeline(LogicalDevice const& ld, ComputePipelineDescriptor const& descriptor);

		static PipelineResourceGroup CreatePipelineResourceGroup(
			LogicalDevice const& ld,
			Pipeline const& pipeline,
			std::uint32_t space,
			std::span<NativePipelineResourceBinding<Backend> const> bindings
		);

		static SchedulerContext CreateScheduler(LogicalDevice const& ld);

		static CompletionToken ExecuteCommands(
			SchedulerContext const& scheduler,
			execution::ExecutionPlan const& plan,
			std::span<PlatformHandle const> presentation_targets,
			std::span<std::reference_wrapper<Resource> const> resources,
			std::span<std::reference_wrapper<View> const> views,
			std::span<std::reference_wrapper<Sampler> const> samplers,
			std::span<std::reference_wrapper<Pipeline> const> pipelines,
			std::span<std::reference_wrapper<PipelineResourceGroup> const> resource_groups,
			execution::StopTokenView stop_token
		);

	};

}
#endif // defined(_WIN32)
