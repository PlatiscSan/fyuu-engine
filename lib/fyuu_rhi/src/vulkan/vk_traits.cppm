/* the vulkan pattern
module;
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
#include <functional>
#include <exception>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>

#include <atomic>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

#include <compare>
#include <format>
#include <ranges>
#include <span>
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
import :pipeline_types;
import :native_pipeline_binding;
import :execution_types;

import plastic.static_hash_table;
import plastic.static_list;
import plastic.lru;

namespace fyuu_rhi::vulkan {

	using namespace fyuu_rhi::pipeline;
	export struct Backend {
#if defined(_WIN32)
		using PlatformHandle = HWND;
#elif defined(__linux__)
		struct X11PlatformHandle {
			Display* display;
			Window window;

			std::strong_ordering operator<=>(X11PlatformHandle const&) const noexcept = default;
		};

		struct WaylandPlatformHandle {
			wl_display* display;
			wl_surface* surface;

			std::strong_ordering operator<=>(WaylandPlatformHandle const&) const noexcept = default;
		};

		using PlatformHandle = std::variant<X11PlatformHandle, WaylandPlatformHandle>;
#elif defined(__ANDROID__)
		using PlatformHandle = ANativeWindow*;
#endif // defined(_WIN32)
		struct PlatformHandleHash {
			std::size_t operator()(PlatformHandle const& value) const noexcept {
#if defined(__linux__)
				return std::visit(
					[](auto const& handle) {
						using Handle = std::remove_cvref_t<decltype(handle)>;
						std::size_t first;
						std::size_t second;
						if constexpr (std::same_as<Handle, X11PlatformHandle>) {
							first = std::hash<Display*>{}(handle.display);
							second = std::hash<Window>{}(handle.window);
						}
						else {
							first = std::hash<wl_display*>{}(handle.display);
							second = std::hash<wl_surface*>{}(handle.surface);
						}
						return first ^ (
							second + 0x9e3779b9u + (first << 6u) + (first >> 2u)
						);
					},
					value
				);
#else
				return std::hash<PlatformHandle>{}(value);
#endif
			}
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

		struct VMAAllocator {
			vk::SharedDevice device;
			VmaAllocator impl;
			~VMAAllocator() noexcept;
		};

		struct PresentationContext {

			struct BackBufferSynchronization {
				vk::SharedSemaphore acquire_semaphore;
				/// @brief	used to signal the vk::aquireImageKHR command if vk::EXTSwapchainMaintenance1 is not supported,
				///			or be submitted by vk::SwapchainPresentFenceInfoKHR.
				vk::SharedFence fence;
				std::uint64_t presentation_id;
			};

			struct SwapChain {
				vk::SharedSurfaceKHR surface;
				vk::SharedSwapchainKHR impl;
				std::vector<vk::Image> back_buffers;
				std::vector<BackBufferSynchronization> sync_objs;
				std::size_t current_back_buffer_index;
				std::size_t last_back_buffer_index;
				std::uint64_t next_presentation_id;
				/// Present queue family pinned at creation. Present batches reserve
				/// this family through QueueRequest::allowed_families, so ownership
				/// transfers on present images never cross families.
				std::uint32_t present_family = VK_QUEUE_FAMILY_IGNORED;
				/// Recreation criteria captured from the presentation request.
				vk::Format format = vk::Format::eUndefined;
				vk::Extent2D extent{ 0u, 0u };
				std::uint32_t buffer_count = 0u;
				vk::PresentModeKHR present_mode = vk::PresentModeKHR::eFifo;
				/// Set by OUT_OF_DATE acquire/present results; recreated on next access.
				bool out_of_date = false;
				bool swapchain_maintenance1_supported;
				bool fifo_latest_ready_supported;
				bool present_id_supported;
			};

			using List = plastic::ds::StaticList<std::pair<PlatformHandle const, SwapChain>, 32u>;
			using HashTable = plastic::ds::StaticHashTable<
				PlatformHandle const,
				List::iterator,
				32u,
				PlatformHandleHash
			>;

			plastic::ds::LRUCache<HashTable, List, 32u> cache;
			std::mutex mutex;

		};

		struct LogicalDevice {
			PhysicalDevice phys_dev;
			QueueAllocator queue_alloc;
			std::unordered_set<vk::StructureType> enabled_features;
			vk::SharedDevice impl;
			std::shared_ptr<vk::detail::DispatchLoaderDynamic> dispatcher;
			std::shared_ptr<VMAAllocator> mem_alloc;
		};

		struct TimelineCompletion {
			vk::SharedSemaphore semaphore;
			std::uint64_t value;
		};

		struct BinaryCompletion {
			vk::SharedFence fence;
		};

		struct BinaryCompletionPointAllocator final
			: public std::enable_shared_from_this<BinaryCompletionPointAllocator> {
			struct ManagedBinaryCompletionPoint {
				std::shared_ptr<BinaryCompletionPointAllocator> owner;
				vk::SharedFence fence;
				/// Semaphores waited by the submission guarded by fence. They become
				/// reusable only after that consumer submission completes.
				std::vector<vk::SharedSemaphore> consumed_semaphores;
				/// Set only after the native submit succeeds. An unsubmitted fence can
				/// never signal and must be destroyed instead of waited or recycled.
				bool submitted = false;
				ManagedBinaryCompletionPoint(
					std::shared_ptr<BinaryCompletionPointAllocator> const& owner_,
					vk::SharedFence const& fence_,
					std::vector<vk::SharedSemaphore>&& consumed_semaphores_
				) noexcept : owner(owner_),
					fence(fence_),
					consumed_semaphores(std::move(consumed_semaphores_)) {
				}
				ManagedBinaryCompletionPoint(ManagedBinaryCompletionPoint const&) = delete;
				ManagedBinaryCompletionPoint& operator=(ManagedBinaryCompletionPoint const&) = delete;
				ManagedBinaryCompletionPoint(ManagedBinaryCompletionPoint&&) noexcept = default;
				ManagedBinaryCompletionPoint& operator=(ManagedBinaryCompletionPoint&&) noexcept = default;
				~ManagedBinaryCompletionPoint() noexcept;
			};
			vk::SharedDevice device;
			std::shared_ptr<vk::detail::DispatchLoaderDynamic> dispatcher;
			std::deque<vk::SharedFence> fences;
			std::deque<vk::SharedSemaphore> semaphores;
			std::mutex mutex;
			ManagedBinaryCompletionPoint AllocateCompletionPoint(std::size_t consumed_semaphore_count);
		};

		using CompletionPoint = std::variant<
			std::monostate,
			TimelineCompletion,
			BinaryCompletion
		>;

		/// Device-level state shared by every scheduler accessing one native
		/// resource. Layout alone is insufficient for exclusive-sharing Vulkan
		/// resources because queue-family ownership persists across graphs.
		struct ResourceState {
			/// Current exclusive owner, or VK_QUEUE_FAMILY_IGNORED before the first
			/// ownership claim and for resources using concurrent sharing.
			std::uint32_t owner_family = VK_QUEUE_FAMILY_IGNORED;
			/// Physical queue that performed the last successful resource access.
			std::optional<QueueIdentifier> last_queue;
			/// Completion of the last successful submission using this resource.
			CompletionPoint completion;
			/// Scheduler phase 1 locks resource states in stable resource-ID order
			/// and retains the locks until phase 3 publishes the new state.
			std::mutex mutex;
		};

		struct Resource {
			struct Buffer {
				std::shared_ptr<VMAAllocator> mem_alloc;
				VkBufferCreateInfo buf_info;
				VkBuffer vk_handle;
				VmaAllocation alloc;
				/// Indirect because ResourceState carries a mutex; each native
				/// resource has exactly one state owner.
				std::unique_ptr<ResourceState> state;
				Buffer(
					std::shared_ptr<VMAAllocator> const& mem_alloc_,
					VkBufferCreateInfo buf_info_,
					VkBuffer vk_handle_,
					VmaAllocation alloc_
				);
				Buffer(Buffer const&) = delete;
				Buffer& operator=(Buffer const&) = delete;
				Buffer(Buffer&& other) noexcept;
				Buffer& operator=(Buffer&& other) noexcept;
				~Buffer() noexcept;
			};
			struct Texture {
				std::shared_ptr<VMAAllocator> mem_alloc;
				VkImageCreateInfo tex_info;
				VkImage vk_handle;
				VmaAllocation alloc;
				/// Stable layout restored after each graph; guarded by state->mutex.
				vk::ImageLayout layout = vk::ImageLayout::eUndefined;
				/// Indirect because ResourceState carries a mutex; each native
				/// resource has exactly one state owner.
				std::unique_ptr<ResourceState> state;
				Texture(
					std::shared_ptr<VMAAllocator> const& mem_alloc_,
					VkImageCreateInfo tex_info_,
					VkImage vk_handle_,
					VmaAllocation alloc_
				);
				Texture(Texture const&) = delete;
				Texture& operator=(Texture const&) = delete;
				Texture(Texture&& other) noexcept;
				Texture& operator=(Texture&& other) noexcept;
				~Texture() noexcept;
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
			/// Legacy render-pass compatibility cannot be queried back from Vulkan.
			/// Preserve the pipeline's attachment signature for recording-time checks.
			std::vector<vk::Format> color_formats;
			vk::Format depth_stencil_format = vk::Format::eUndefined;
			vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
			vk::SharedPipeline impl;
		};

		struct PipelineResourceGroup {
			std::uint32_t space = 0u;
			vk::SharedDescriptorPool pool;
			vk::DescriptorSet set;
			vk::SharedPipelineLayout layout;
		};

		struct SchedulerContext {
			/// One command pool exists per queue family first used by this scheduler.
			/// The pool and its command buffers are externally synchronized together.
			struct CommandPoolContext {
				vk::SharedCommandPool impl;
				std::deque<vk::CommandBuffer> command_buffers;
				std::mutex mutex;
			};

			struct Implementation {

				using TimelineMap = std::unordered_map<QueueIdentifier, TimelineCompletion>;

				vk::SharedPhysicalDevice physical_device;
				vk::SharedDevice device;
				std::shared_ptr<vk::detail::DispatchLoaderDynamic> dispatcher;
				/// QueueAllocator copies share the device-level queue registry and load
				/// accounting. No queue is reserved by SchedulerContext construction.
				QueueAllocator queue_allocator;
				/// Entries are inserted on first use. Node-based maps keep contexts with
				/// mutexes and Vulkan handles at stable addresses.
				std::unordered_map<std::uint32_t, CommandPoolContext> command_pools;
				std::variant<std::monostate, std::shared_ptr<BinaryCompletionPointAllocator>, TimelineMap> completion_points;
				/// Swap chains and their acquire/present synchronization are scheduler
				/// private and are populated only by presentation work in phase 1.
				/// Keyed by present queue family: a swap chain is pinned to one family
				/// at creation, while the assigned queue index may vary per transaction.
				std::unordered_map<std::uint32_t, PresentationContext> presentations;
				/// Serializes phase 1 and phase 3 state publication for this scheduler.
				/// Batch command recording between those phases remains parallel.
				std::mutex execution_mutex;
				/// Selects timeline points or the per-edge semaphore/per-batch fence
				/// fallback. Neither path creates a synchronization object eagerly.
				bool timeline_semaphore_supported;
				bool synchronization2_supported;
				/// Rendering is independently selected from synchronization and
				/// completion. Keeping the feature bit here makes all eight feature
				/// combinations follow the same scheduler implementation.
				bool dynamic_rendering_supported;

				Implementation(
					vk::SharedPhysicalDevice const& physical_device_,
					vk::SharedDevice const& device_,
					std::shared_ptr<vk::detail::DispatchLoaderDynamic> const& dispatcher_,
					QueueAllocator const& queue_allocator_
				) : physical_device(physical_device_),
					device(device_),
					dispatcher(dispatcher_),
					queue_allocator(queue_allocator_),
					timeline_semaphore_supported(false),
					synchronization2_supported(false),
					dynamic_rendering_supported(false) {}
			};

			std::shared_ptr<Implementation> impl;

			std::strong_ordering operator<=>(SchedulerContext const& other) const noexcept = default;
		};

		class CompletionToken final {
			/// Defined in the execution partition, where Implementation is complete.
			/// A custom deleter keeps destruction independent of that completeness
			/// while avoiding the shared_ptr control block: the implementation is
			/// owned exclusively by the token and never shared.
			struct Implementation;
			struct Deleter {
				void operator()(Implementation* impl) const noexcept;
			};
			std::unique_ptr<Implementation, Deleter> impl;

			explicit CompletionToken(std::unique_ptr<Implementation, Deleter>&& impl_) noexcept
				: impl(std::move(impl_)) {}
			friend struct Backend;

		public:
			CompletionToken() noexcept = default;
			CompletionToken(CompletionToken const&) = delete;
			CompletionToken& operator=(CompletionToken const&) = delete;
			CompletionToken(CompletionToken&&) noexcept = default;
			CompletionToken& operator=(CompletionToken&&) noexcept = default;
			~CompletionToken() noexcept;

			[[nodiscard]] bool Poll() noexcept;
			[[nodiscard]] std::exception_ptr Error() const noexcept;
			[[nodiscard]] bool IsStopped() const noexcept;
		};

		static Instance CreateInstance(
			std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver
#if defined(__ANDROID__)
			, android_app* android_app
#endif // defined(__ANDROID__)
		);

		static std::vector<PhysicalDevice> EnumeratePhysicalDevices(Instance const& instance);

		static PhysicalDeviceInfo GetPhysicalDeviceInfo(PhysicalDevice const& phys_dev);

		static LogicalDevice CreateLogicalDevice(PhysicalDevice const& phys_dev);

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
#endif // !defined(__APPLE__)
