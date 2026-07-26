/* the d3d12 pattern
module;
#include <boost/smart_ptr/intrusive_ptr.hpp>
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
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>
#include <variant>
#include <string_view>
#include <span>
#include <cstdint>
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
import :d3d12_utility;
import :d3d12_descriptor_allocator;
import :d3d12_device_removal_tracker;
import :sampler_types;
import :scheduler_types;
import :pipeline_types;
import :native_pipeline_binding;
import :native_command_graph;
import :presentation_cache;
import :completion_service;
import :execution_pool;

namespace fyuu_rhi::d3d12 {

	using namespace fyuu_rhi::pipeline;
	using namespace fyuu_rhi::execution;

	export struct Backend {
		using PresentationTarget = HWND;
		struct PresentationTargetHash {
			std::size_t operator()(PresentationTarget target) const noexcept {
				return execution::HashNativePointer(target);
			}
		};

		using Instance = Microsoft::WRL::ComPtr<IDXGIFactory2>;
		using PhysicalDevice = Microsoft::WRL::ComPtr<IDXGIAdapter1>;

		struct LogicalDevice {
			struct PresentationEntry {
				struct FrameSlot {
					Microsoft::WRL::ComPtr<ID3D12Resource> back_buffer;
					std::uint64_t fence_value = 0u;
				};

				Microsoft::WRL::ComPtr<IDXGISwapChain3> swapchain;
				Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue;
				DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
				std::uint32_t width = 0u;
				std::uint32_t height = 0u;
				std::vector<FrameSlot> frames;
				std::shared_ptr<std::mutex> mutex = std::make_shared<std::mutex>();
			};

			using PresentationCache = execution::PresentationCache<
				PresentationTarget,
				PresentationEntry,
				PresentationTargetHash
			>;

			Microsoft::WRL::ComPtr<IDXGIAdapter1> phys_dev;
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
			std::shared_ptr<PresentationCache> presentation_cache;
			boost::intrusive_ptr<execution::CompletionService> completion_service;
		};

		struct Scheduler {
			struct CommandEntry {
				Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
				Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> impl;
				bool closed = false;
			};

			using CommandPool = execution::ExecutionPool<CommandEntry>;

			struct QueueState {
				Microsoft::WRL::ComPtr<ID3D12Device> device;
				Microsoft::WRL::ComPtr<ID3D12CommandQueue> impl;
				Microsoft::WRL::ComPtr<ID3D12Fence> fence;
				std::atomic_uint64_t next_fence_value = 1u;
				std::shared_ptr<CommandPool> command_pool;
				D3D12_COMMAND_LIST_TYPE type = D3D12_COMMAND_LIST_TYPE_DIRECT;
				std::mutex submission_mutex;
			};

			struct QueueCollection {
				std::shared_ptr<QueueState> graphics;
				std::shared_ptr<QueueState> compute;
				std::shared_ptr<QueueState> copy;

				[[nodiscard]] std::shared_ptr<QueueState> const& Select(
					execution::GraphNodeFlagBits capability
				) const;
			};

			struct Implementation {
				QueueCollection queues;
				Microsoft::WRL::ComPtr<IDXGIAdapter1> physical_device;
				std::shared_ptr<LogicalDevice::PresentationCache> presentation_cache;
				boost::intrusive_ptr<execution::CompletionService> completion_service;
			};
			std::shared_ptr<Implementation> impl;
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

			Microsoft::WRL::ComPtr<D3D12MA::Allocation> allocation;
			ManagedDescriptorHandle impl;
			Type type;
			std::uint32_t base_mip_level = 0u;
			std::uint32_t mip_level_count = 1u;
			std::uint32_t base_array_layer = 0u;
			std::uint32_t array_layer_count = 1u;
			DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

			[[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CPU() const noexcept {
				return impl.CPU();
			}

			[[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GPU() const noexcept {
				return impl.GPU();
			}

			[[nodiscard]] ID3D12Resource* Resource() const noexcept {
				return allocation->GetResource();
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
		using CommandGraph = std::shared_ptr<execution::NativeCommandGraph<Backend>>;
		using ExecutableGraph = std::shared_ptr<execution::NativeExecutableGraph<Backend>>;
		struct GraphExecution {
			struct Batch {
				struct PresentationRequest {
					PresentationTarget target;
					execution::GraphResourceID source_id;
					Microsoft::WRL::ComPtr<ID3D12Resource> source;
					D3D12_RESOURCE_STATES source_state = D3D12_RESOURCE_STATE_COMMON;
					bool vertical_sync = true;
					std::uint32_t frames_in_flight = 3u;
				};

				struct InFlightPresentation {
					LogicalDevice::PresentationCache::Lease entry;
					Scheduler::CommandPool::Lease commands;
					std::uint32_t frame_index;

					InFlightPresentation(
						LogicalDevice::PresentationCache::Lease&& entry_,
						Scheduler::CommandPool::Lease&& commands_,
						std::uint32_t frame_index_
					) noexcept : entry(std::move(entry_)),
						commands(std::move(commands_)),
						frame_index(frame_index_) {

					}
				};

				std::shared_ptr<Scheduler::QueueState> queue;
				Scheduler::CommandPool::Lease commands;
				std::uint64_t fence_value = 0u;
				std::vector<PresentationRequest> presentation_requests;
				std::vector<InFlightPresentation> in_flight_presentations;

				Batch(
					std::shared_ptr<Scheduler::QueueState> const& queue_,
					Scheduler::CommandPool::Lease&& commands_,
					std::uint64_t fence_value_
				) noexcept : queue(queue_),
					commands(std::move(commands_)),
					fence_value(fence_value_) {

				}

				Batch(Batch const&) = delete;
				Batch& operator=(Batch const&) = delete;
				Batch(Batch&&) noexcept = default;
				Batch& operator=(Batch&&) noexcept = default;
			};

			Scheduler scheduler;
			ExecutableGraph graph;
			std::vector<Batch> batches;
		};

		static Microsoft::WRL::ComPtr<IDXGIFactory2> CreateInstance(std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver);

		static std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter1>> EnumeratePhysicalDevices(Microsoft::WRL::ComPtr<IDXGIFactory2> const& factory);

		static PhysicalDeviceInfo GetPhysicalDeviceInfo(Microsoft::WRL::ComPtr<IDXGIAdapter1> const& adapter);

		static LogicalDevice CreateLogicalDevice(Microsoft::WRL::ComPtr<IDXGIAdapter1> const& adapter);

		static Scheduler CreateScheduler(LogicalDevice const& ld, SchedulerDescriptor const& descriptor);
		static CommandGraph CreateCommandGraph(
			execution::CommandGraphDescriptor const& descriptor
		);
		static ExecutableGraph CompileCommandGraph(CommandGraph const& graph);

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

	};

	Backend::GraphExecution CreateGraphExecution(
		Backend::Scheduler const& scheduler,
		Backend::ExecutableGraph const& graph
	);
	void StartGraphExecution(
		Backend::GraphExecution& graph_execution,
		execution::GraphCompletion const& completion
	);
	void StartSchedulerExecution(
		Backend::Scheduler const& scheduler,
		execution::SchedulerCompletion const& completion
	);
	void StartDeferredDestroy(
		Backend::Scheduler const& scheduler,
		execution::DeferredDestroy const& deferred_destroy
	);
	void StartMapResource(
		Backend::Scheduler const& scheduler,
		Backend::Resource& resource,
		execution::ResourceMapRequest const& request
	);
	void StartUnmapResource(
		Backend::Scheduler const& scheduler,
		Backend::Resource& resource,
		execution::ResourceUnmapRequest const& request
	);

}
#endif // defined(_WIN32)
