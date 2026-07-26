/* the webgpu pattern
module;
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <version>
#if !defined(__cpp_lib_modules)

#endif // !defined(__cpp_lib_modules)
#include <webgpu/webgpu_cpp.h>
export module fyuu_rhi:;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
namespace fyuu_rhi::webgpu {

}

*/

module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <atomic>
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <variant>
#include <format>
#include <span>
#include <cstdint>
#include <utility>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <wayland-client-core.h>
#include <wayland-util.h>
#elif defined(__ANDROID__)
#include <android/native_window.h>
#include <android/android_native_app_glue.h>
#endif // defined(_WIN32)
#include <webgpu/webgpu_cpp.h>
export module fyuu_rhi:webgpu_traits;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core_types;
import :resource_types;
import :sampler_types;
import :scheduler_types;
import :pipeline_types;
import :native_pipeline_binding;
import :native_command_graph;
import :presentation_cache;
import :completion_service;

namespace fyuu_rhi::webgpu {
	using namespace fyuu_rhi::pipeline;
	using namespace fyuu_rhi::execution;

	struct Backend {
#if defined(_WIN32)
		using PresentationTarget = HWND;
#elif defined(__linux__)
		struct X11PresentationTarget {
			Display* display;
			Window window;

			friend auto operator<=>(X11PresentationTarget const&, X11PresentationTarget const&) noexcept = default;
		};

		struct WaylandPresentationTarget {
			wl_display* display;
			wl_surface* surface;

			friend auto operator<=>(WaylandPresentationTarget const&, WaylandPresentationTarget const&) noexcept = default;
		};

		using PresentationTarget = std::variant<X11PresentationTarget, WaylandPresentationTarget>;
#elif defined(__ANDROID__)
		using PresentationTarget = ANativeWindow*;
#endif
		struct PresentationTargetHash {
#if defined(_WIN32) || defined(__ANDROID__)
			std::size_t operator()(PresentationTarget target) const noexcept {
				return execution::HashNativePointer(target);
			}
#elif defined(__linux__)
			std::size_t operator()(PresentationTarget const& target) const noexcept {
				if (auto value = std::get_if<X11PresentationTarget>(&target)) {
					auto display = execution::HashNativePointer(value->display);
					auto window = std::hash<Window>{}(value->window);
					return execution::CombineHashes(display, window);
				}
				auto const& value = std::get<WaylandPresentationTarget>(target);
				auto display = execution::HashNativePointer(value.display);
				auto surface = execution::HashNativePointer(value.surface);
				return execution::CombineHashes(display, surface);
			}
#endif
		};
		using Instance = wgpu::Instance;
		using PhysicalDevice = wgpu::Adapter;
		using Surface = wgpu::Surface;
		struct LogicalDevice {
			struct PresentationEntry {
				using FrameSlot = std::atomic_bool;

				struct FrameState {
					std::vector<std::shared_ptr<FrameSlot>> slots;
					std::size_t next_slot = 0u;
					std::mutex mutex;
				};

				wgpu::Instance instance;
				wgpu::Surface surface;
				wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
				wgpu::PresentMode present_mode = wgpu::PresentMode::Fifo;
				std::uint32_t width = 0u;
				std::uint32_t height = 0u;
				bool vertical_sync = true;
				std::uint32_t frames_in_flight = 0u;
				std::shared_ptr<FrameState> frames;
			};

			using PresentationCache = execution::PresentationCache<
				PresentationTarget,
				PresentationEntry,
				PresentationTargetHash
			>;

			wgpu::Device impl;
			wgpu::Adapter adapter;
			std::shared_ptr<PresentationCache> presentation_cache;
			boost::intrusive_ptr<execution::CompletionService> completion_service;

			wgpu::Queue GetQueue() const {
				return impl.GetQueue();
			}
		};

		struct WebGPUScheduler {
			struct QueueState {
				wgpu::Device device;
				wgpu::Queue impl;
			};

			struct QueueCollection {
				std::shared_ptr<QueueState> graphics;
				std::shared_ptr<QueueState> compute;
				std::shared_ptr<QueueState> copy;

				[[nodiscard]] std::shared_ptr<QueueState> const& Select(
					execution::GraphNodeFlagBits capability
				) const;
			};

			QueueCollection queues;
			wgpu::Adapter adapter;
			std::shared_ptr<LogicalDevice::PresentationCache> presentation_cache;
			boost::intrusive_ptr<execution::CompletionService> completion_service;
		};

		using Scheduler = std::shared_ptr<WebGPUScheduler>;

		using Resource = std::variant<std::monostate, wgpu::Buffer, wgpu::Texture>;

		struct BufferView {
			wgpu::Buffer buf;
			std::size_t offset;
			std::size_t range;
		};
		using View = std::variant<std::monostate, wgpu::TextureView, BufferView>;

		using Sampler = wgpu::Sampler;

		struct Pipeline {
			std::vector<wgpu::BindGroupLayout> bind_group_layouts;
			std::vector<PipelineBindingMetadata> bindings;
			std::variant<wgpu::RenderPipeline, wgpu::ComputePipeline> impl;
			bool compute = false;
		};

		struct PipelineResourceGroup {
			NativePipelineResourceGroup<Backend> native;
			wgpu::BindGroup impl;
			std::uint32_t space = 0u;
		};
		using CommandGraph = std::shared_ptr<execution::NativeCommandGraph<Backend>>;
		using ExecutableGraph = std::shared_ptr<execution::NativeExecutableGraph<Backend>>;
		struct GraphExecution {
			struct InFlightPresentation {
				LogicalDevice::PresentationCache::Lease entry;
				std::shared_ptr<LogicalDevice::PresentationEntry::FrameSlot> frame;
			};

			struct Batch {
				std::shared_ptr<WebGPUScheduler::QueueState> queue;
				wgpu::CommandBuffer commands;
				std::vector<InFlightPresentation> presentations;

				Batch(
					std::shared_ptr<WebGPUScheduler::QueueState> const& queue_,
					wgpu::CommandBuffer const& commands_,
					std::vector<InFlightPresentation>&& presentations_
				) noexcept : queue(queue_), commands(commands_),
					presentations(std::move(presentations_)) {

				}
				Batch(Batch const&) = delete;
				Batch& operator=(Batch const&) = delete;
				Batch(Batch&& other) noexcept
					: queue(other.queue), commands(other.commands),
					presentations(std::move(other.presentations)) {

				}
				Batch& operator=(Batch&& other) noexcept {
					queue = other.queue;
					commands = other.commands;
					presentations = std::move(other.presentations);
					return *this;
				}
			};

			Scheduler scheduler;
			ExecutableGraph graph;
			std::vector<Batch> batches;
		};

		static wgpu::Instance CreateInstance(
			std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver
#if defined(__ANDROID__)
			, android_app* android_app
#endif // defined(__ANDROID__)
		);

		static wgpu::Adapter EnumeratePhysicalDevices(wgpu::Instance const& instance);

#if defined(_WIN32)
		static wgpu::Surface CreateSurface(wgpu::Instance const& instance, HWND window_handle);
#elif defined(__linux__)
		static wgpu::Surface CreateSurface(wgpu::Instance const& instance, Display* x11_dpy, Window x11_window);
		static wgpu::Surface CreateSurface(wgpu::Instance const& instance, wl_display* display, wl_surface* surface);
#elif defined(__ANDROID__)
		static wgpu::Surface CreateSurface(wgpu::Instance const& instance, ANativeWindow* window);
#endif // defined(_WIN32)
		static PhysicalDeviceInfo GetPhysicalDeviceInfo(wgpu::Adapter const& phys_dev);

		static LogicalDevice CreateLogicalDevice(wgpu::Adapter const& adapter);

		static Scheduler CreateScheduler(LogicalDevice const& ld, SchedulerDescriptor const& descriptor);
		static CommandGraph CreateCommandGraph(
			execution::CommandGraphDescriptor const& descriptor
		);
		static ExecutableGraph CompileCommandGraph(CommandGraph const& graph);

		static Resource CreateBuffer(LogicalDevice const& ld, std::size_t size_in_bytes, ResourceFlags const& flags);

		static Resource CreateTexture(LogicalDevice const& ld, std::size_t width, std::size_t height, std::size_t depth_arr_layers, std::size_t mip_lvl_cnt, ResourceFlags const& flags);

		static View CreateTextureView(LogicalDevice const& ld, Resource const& res, std::size_t base_mip_lvl, std::size_t mip_lvl_cnt, std::size_t base_arr_layer, std::size_t arr_layer_cnt, ResourceFlags const& flags);

		static View CreateBufferView(LogicalDevice const& ld, Resource const& buf, std::size_t offset, std::size_t range, ResourceFlags const& flags);

		static wgpu::Sampler CreateSampler(LogicalDevice const& ld, SamplerDescriptor const& desc);

		static Pipeline CreateGraphicsPipeline(LogicalDevice const& ld, GraphicsPipelineDescriptor const& descriptor);
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
