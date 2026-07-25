/* the vulkan pattern
module;
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <version>
#if !defined(__cpp_lib_modules)

#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)

#endif //!defined(__APPLE__)
export module fyuu_rhi:;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
namespace fyuu_rhi::vulkan {

}
#endif // !defined(__APPLE__)

*/

module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <type_traits>
#include <memory>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <string_view>
#include <optional>
#include <variant>
#include <format>
#include <ranges>
#include <span>
#include <utility>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
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
#include <vma/vk_mem_alloc.h>
#include <boost/scope/unique_resource.hpp>


#endif //!defined(__APPLE__)
export module fyuu_rhi:vulkan_traits;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import vulkan;
import :core_types;
import :vulkan_queue_allocator;
import :resource_types;
import :sampler_types;
import :scheduler_types;
import :pipeline_types;
import :native_pipeline_binding;
import :native_command_graph;
import :presentation_cache;
import :completion_service;
import :execution_pool;

namespace fyuu_rhi::vulkan {

	using namespace fyuu_rhi::pipeline;
	using namespace fyuu_rhi::execution;

	export struct Backend {

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
			struct HashTarget {
				std::size_t operator()(X11PresentationTarget const& target) const noexcept {
					auto display = execution::HashNativePointer(target.display);
					auto window = std::hash<Window>{}(target.window);
					return execution::CombineHashes(display, window);
				}

				std::size_t operator()(WaylandPresentationTarget const& target) const noexcept {
					auto display = execution::HashNativePointer(target.display);
					auto surface = execution::HashNativePointer(target.surface);
					return execution::CombineHashes(display, surface);
				}
			};

			std::size_t operator()(PresentationTarget const& target) const noexcept {
				return std::visit(HashTarget{}, target);
			}
#endif
		};

		struct Instance {
			vk::detail::DispatchLoaderDynamic const& dispatcher;
			std::vector<std::string> enabled_extensions;
			vk::SharedInstance impl;
			vk::SharedDebugUtilsMessengerEXT debug_messenger;
		};
		
		struct PhysicalDevice {
			Instance const& instance;
			vk::SharedPhysicalDevice impl;
		};

		using Surface = vk::SharedSurfaceKHR;

		struct VMAAllocator {
			vk::SharedDevice device;
			VmaAllocator impl;

			VMAAllocator(vk::SharedDevice const& device_, VmaAllocator impl_) noexcept
				: device(device_), impl(impl_) {

			}

			~VMAAllocator() noexcept {
				if (impl) {
					vmaDestroyAllocator(impl);
				}
			}
		};

		struct LogicalDevice {
			struct PresentationEntry {
				struct FrameSlot {
					vk::Image image;
					vk::SharedSemaphore render_finished;
					bool initialized = false;
				};

				vk::SharedSurfaceKHR surface;
				vk::SharedSwapchainKHR swapchain;
				std::vector<FrameSlot> frames;
				vk::Format format = vk::Format::eUndefined;
				vk::ColorSpaceKHR color_space = vk::ColorSpaceKHR::eSrgbNonlinear;
				vk::Extent2D extent;
				vk::PresentModeKHR present_mode = vk::PresentModeKHR::eFifo;
				std::vector<vk::PresentModeKHR> compatible_present_modes;
				std::uint32_t requested_frames_in_flight = 3u;
				std::shared_ptr<std::mutex> mutex = std::make_shared<std::mutex>();
			};

			using PresentationCache = execution::PresentationCache<
				PresentationTarget,
				PresentationEntry,
				PresentationTargetHash
			>;

			PhysicalDevice phys_dev;
			QueueAllocator queue_alloc;
			std::vector<std::string> enabled_extensions;
			std::unordered_set<vk::StructureType> enabled_features;
			vk::SharedDevice impl;
			std::shared_ptr<vk::detail::DispatchLoaderDynamic> dispatcher;
			std::shared_ptr<VMAAllocator> mem_alloc;
			std::shared_ptr<PresentationCache> presentation_cache;
			boost::intrusive_ptr<execution::CompletionService> completion_service;
		};

		struct VulkanScheduler {
			struct CommandEntry {
				vk::SharedCommandPool command_pool;
				vk::SharedCommandBuffer impl;
				bool recording = false;
			};

			using CommandPool = execution::ExecutionPool<CommandEntry>;
			struct TimelineSynchronization {
				vk::SharedSemaphore semaphore;
				std::atomic_uint64_t next_value = 1u;

				explicit TimelineSynchronization(vk::SharedSemaphore const& semaphore_) noexcept
					: semaphore(semaphore_) {

				}

				TimelineSynchronization(TimelineSynchronization const&) = delete;
				TimelineSynchronization& operator=(TimelineSynchronization const&) = delete;
				TimelineSynchronization(TimelineSynchronization&& other) noexcept
					: semaphore(other.semaphore),
					next_value(other.next_value.exchange(0u, std::memory_order_relaxed)) {

				}
				TimelineSynchronization& operator=(TimelineSynchronization&&) = delete;
			};

			struct BinarySynchronizationPool {
				using SemaphorePool = execution::ExecutionPool<vk::SharedSemaphore>;
				using FencePool = execution::ExecutionPool<vk::SharedFence>;

				vk::SharedDevice device;
				std::shared_ptr<vk::detail::DispatchLoaderDynamic> dispatcher;
				std::shared_ptr<SemaphorePool> semaphores;
				std::shared_ptr<FencePool> fences;

				BinarySynchronizationPool(
					vk::SharedDevice const& device_,
					std::shared_ptr<vk::detail::DispatchLoaderDynamic> const& dispatcher_
				) noexcept : device(device_),
					dispatcher(dispatcher_),
					semaphores(std::make_shared<SemaphorePool>()),
					fences(std::make_shared<FencePool>()) {

				}

				[[nodiscard]] SemaphorePool::Lease AcquireSemaphore();
				[[nodiscard]] FencePool::Lease AcquireFence();
			};

			struct QueueState {
				vk::SharedDevice device;
				std::shared_ptr<vk::detail::DispatchLoaderDynamic> dispatcher;
				ManagedQueue allocation;
				vk::SharedQueue impl;
				std::variant<
					TimelineSynchronization,
					std::shared_ptr<BinarySynchronizationPool>
				> synchronization;
				std::shared_ptr<CommandPool> command_pool;
				CommandQueueType capability = CommandQueueType::None;
				std::uint32_t family = 0u;
				std::uint32_t index = 0u;
				std::shared_ptr<std::mutex> submission_mutex = std::make_shared<std::mutex>();
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
				PhysicalDevice physical_device;
				std::shared_ptr<LogicalDevice::PresentationCache> presentation_cache;
				std::shared_ptr<BinarySynchronizationPool> presentation_synchronization;
				std::unordered_set<std::string> enabled_extensions;
				std::unordered_set<vk::StructureType> enabled_features;
				boost::intrusive_ptr<execution::CompletionService> completion_service;
			};
			std::shared_ptr<Implementation> impl;
		};

		using Scheduler = std::shared_ptr<VulkanScheduler>;

		struct Resource {
			struct Buffer {
				std::shared_ptr<VMAAllocator> mem_alloc;
				VkBufferCreateInfo buf_info;
				VkBuffer vk_handle;
				VmaAllocation alloc;
				VmaAllocationInfo alloc_info;
				Buffer(
					std::shared_ptr<VMAAllocator> const& mem_alloc_,
					VkBufferCreateInfo buf_info_,
					VkBuffer vk_handle_,
					VmaAllocation alloc_,
					VmaAllocationInfo alloc_info_
				) noexcept : mem_alloc(mem_alloc_), buf_info(buf_info_),
					vk_handle(vk_handle_), alloc(alloc_), alloc_info(alloc_info_) {}
				Buffer(Buffer const&) = delete;
				Buffer& operator=(Buffer const&) = delete;
				Buffer(Buffer&& other) noexcept
					: mem_alloc(std::move(other.mem_alloc)), buf_info(other.buf_info),
					vk_handle(std::exchange(other.vk_handle, nullptr)),
					alloc(std::exchange(other.alloc, nullptr)), alloc_info(other.alloc_info) {}
				Buffer& operator=(Buffer&& other) noexcept {
					std::swap(mem_alloc, other.mem_alloc);
					std::swap(buf_info, other.buf_info);
					std::swap(vk_handle, other.vk_handle);
					std::swap(alloc, other.alloc);
					std::swap(alloc_info, other.alloc_info);
					return *this;
				}
				~Buffer() noexcept {
					if (mem_alloc && vk_handle && alloc) {
						vmaDestroyBuffer(mem_alloc->impl, vk_handle, alloc);
					}
				}
			};
			struct Texture {
				std::shared_ptr<VMAAllocator> mem_alloc;
				VkImageCreateInfo buf_info;
				VkImage vk_handle;
				VmaAllocation alloc;
				VmaAllocationInfo alloc_info;
				vk::ImageLayout last_layout;
				mutable std::atomic<vk::ImageLayout> curr_layout;
				Texture(
					std::shared_ptr<VMAAllocator> const& mem_alloc_,
					VkImageCreateInfo buf_info_,
					VkImage vk_handle_,
					VmaAllocation alloc_,
					VmaAllocationInfo alloc_info_,
					vk::ImageLayout last_layout_,
					vk::ImageLayout curr_layout_
				) noexcept : mem_alloc(mem_alloc_), buf_info(buf_info_),
					vk_handle(vk_handle_), alloc(alloc_), alloc_info(alloc_info_),
					last_layout(last_layout_), curr_layout(curr_layout_) {}
				Texture(Texture const&) = delete;
				Texture& operator=(Texture const&) = delete;
				Texture(Texture&& other) noexcept
					: mem_alloc(std::move(other.mem_alloc)), buf_info(other.buf_info),
					vk_handle(std::exchange(other.vk_handle, nullptr)),
					alloc(std::exchange(other.alloc, nullptr)), alloc_info(other.alloc_info),
					last_layout(other.last_layout),
					curr_layout(other.curr_layout.load(std::memory_order_relaxed)) {}
				Texture& operator=(Texture&& other) noexcept {
					std::swap(mem_alloc, other.mem_alloc);
					std::swap(buf_info, other.buf_info);
					std::swap(vk_handle, other.vk_handle);
					std::swap(alloc, other.alloc);
					std::swap(alloc_info, other.alloc_info);
					std::swap(last_layout, other.last_layout);
					auto layout = curr_layout.exchange(
						other.curr_layout.load(std::memory_order_relaxed),
						std::memory_order_relaxed
					);
					other.curr_layout.store(layout, std::memory_order_relaxed);
					return *this;
				}
				~Texture() noexcept {
					if (mem_alloc && vk_handle && alloc) {
						vmaDestroyImage(mem_alloc->impl, vk_handle, alloc);
					}
				}
			};
			std::variant<std::monostate, Buffer, Texture> impl;
		};

		struct View {
			struct Buffer {
				vk::BufferViewCreateInfo info;
				vk::SharedBufferView impl;
			};
			struct Texture {
				vk::ImageViewCreateInfo info;
				vk::SharedImageView impl;
			};
			std::variant<std::monostate, Buffer, Texture> impl;
		};

		using Sampler = vk::SharedSampler;

		struct Pipeline {
			vk::PipelineBindPoint bind_point = vk::PipelineBindPoint::eGraphics;
			std::vector<vk::SharedDescriptorSetLayout> descriptor_set_layouts;
			std::vector<PipelineBindingMetadata> bindings;
			vk::SharedPipelineLayout layout;
			vk::SharedRenderPass compatible_render_pass;
			vk::SharedPipeline impl;
		};

		struct PipelineResourceGroup {
			std::uint32_t space = 0u;
			vk::SharedDescriptorPool pool;
			vk::DescriptorSet set;
			vk::SharedPipelineLayout layout;
		};
		using CommandGraph = std::shared_ptr<execution::NativeCommandGraph<Backend>>;
		using ExecutableGraph = std::shared_ptr<execution::NativeExecutableGraph<Backend>>;
		struct GraphExecution {
			struct Batch {
				struct PresentationRequest {
					PresentationTarget target;
					execution::GraphResourceID source_id;
					Backend::Resource::Texture const* source;
					bool vertical_sync = true;
					std::uint32_t frames_in_flight = 3u;
				};

				struct InFlightPresentation {
					LogicalDevice::PresentationCache::Lease entry;
					VulkanScheduler::CommandPool::Lease commands;
					VulkanScheduler::BinarySynchronizationPool::SemaphorePool::Lease image_available;
					std::optional<
						VulkanScheduler::BinarySynchronizationPool::FencePool::Lease
					> present_fence;
				};

				std::shared_ptr<VulkanScheduler::QueueState> queue;
				VulkanScheduler::CommandPool::Lease commands;
				std::vector<vk::SharedRenderPass> render_passes;
				std::vector<vk::SharedFramebuffer> framebuffers;
				std::optional<VulkanScheduler::BinarySynchronizationPool::FencePool::Lease> fence;
				std::uint64_t synchronization_value = 0u;
				std::vector<PresentationRequest> presentation_requests;
				std::vector<InFlightPresentation> in_flight_presentations;
			};
			struct BinaryDependency {
				execution::SubmissionBatchID source;
				execution::SubmissionBatchID destination;
				VulkanScheduler::BinarySynchronizationPool::SemaphorePool::Lease semaphore;
			};

			Scheduler scheduler;
			ExecutableGraph graph;
			std::vector<Batch> batches;
			std::vector<BinaryDependency> binary_dependencies;
		};

		static Instance CreateInstance(
			std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver
#if defined(__ANDROID__)
			, android_app* android_app
#endif // defined(__ANDROID__)
		);

		static std::vector<PhysicalDevice> EnumeratePhysicalDevices(Instance const& instance);

#if defined(_WIN32)
		static vk::SharedSurfaceKHR CreateSurface(Instance const& instance, HWND window_handle);
#elif defined(__linux__)
		static vk::SharedSurfaceKHR CreateSurface(Instance const& instance, Display* x11_dpy, Window x11_window);
		static vk::SharedSurfaceKHR CreateSurface(Instance const& instance, wl_display* display, wl_surface* surface);
#elif defined(__ANDROID__)
		static vk::SharedSurfaceKHR CreateSurface(Instance const& instance, ANativeWindow* window);
#endif // defined(_WIN32)

		static PhysicalDeviceInfo GetPhysicalDeviceInfo(PhysicalDevice const& phys_dev);

		static LogicalDevice CreateLogicalDevice(PhysicalDevice const& phys_dev);

		static Scheduler CreateScheduler(LogicalDevice& ld, SchedulerDescriptor const& descriptor);
		static CommandGraph CreateCommandGraph(
			execution::CommandGraphDescriptor const& descriptor,
			execution::NativeCommandGraphBindings<Backend> const& bindings
		);

		static Resource CreateBuffer(LogicalDevice const& ld, std::size_t size_in_bytes, ResourceFlags const& flags);

		static Resource CreateTexture(LogicalDevice const& ld, std::size_t width, std::size_t height, std::size_t depth_arr_layers, std::size_t mip_lvl_cnt, ResourceFlags const& flags);

		static View CreateTextureView(LogicalDevice const& ld, Resource const& res, std::size_t base_mip_lvl, std::size_t mip_lvl_cnt, std::size_t base_arr_layer, std::size_t arr_layer_cnt, ResourceFlags const& flags);

		static View CreateBufferView(LogicalDevice const& ld, Resource const& buf, std::size_t offset, std::size_t range, ResourceFlags const& flags);

		static vk::SharedSampler CreateSampler(LogicalDevice const& ld, SamplerDescriptor const& descriptor);

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
	Backend::ExecutableGraph CompileCommandGraph(Backend::CommandGraph const& graph);
	void StartGraphExecution(
		Backend::GraphExecution& graph_execution,
		execution::GraphCompletion const& completion
	);
}
#endif // !defined(__APPLE__)
