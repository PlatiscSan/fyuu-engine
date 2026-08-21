module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <deque>
#include <exception>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>

#include <cstdint>
#include <unordered_set>
#include <mutex>

#include <optional>
#include <variant>
#include <string_view>

#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)
#define FYUU_RHI_USE_VULKAN_HEADER
#include <vulkan/vulkan_shared.hpp>
#endif // !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)

module fyuu_rhi:vulkan_data;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
#if !defined(FYUU_RHI_USE_VULKAN_HEADER)
import vulkan;
#endif // !defined(FYUU_RHI_USE_VULKAN_HEADER)
import :pipeline;
import :execution;
import :resource;
import :vulkan_memory_allocator;
import :vulkan_queue_allocator;
import plastic.static_hash_table;
import plastic.static_list;
import plastic.lru;

namespace fyuu_rhi::vulkan {

	struct Instance {
		std::unordered_set<std::string_view> enabled_extensions;
		std::unordered_set<std::string_view> enabled_layers;
		vk::SharedInstance impl;
		std::shared_ptr<vk::detail::DispatchLoaderDynamic> dispatcher; 
		vk::SharedDebugUtilsMessengerEXT debug_messenger;
	};

	/// Instance extensions that were actually enabled, needed to gate device
	/// extensions that depend on an instance-level extension (e.g. swapchain
	/// maintenance). The VkInstance handle itself is recoverable from impl via
	/// getDestructorType(), so only the enabled names are retained here.
	struct PhysicalDevice {
		vk::SharedPhysicalDevice impl;
		std::shared_ptr<vk::detail::DispatchLoaderDynamic> dispatcher;
		std::unordered_set<std::string_view> enabled_instance_extensions;
	};

	struct LogicalDevice {
		vk::SharedPhysicalDevice physical_device;
		QueueAllocator queue_alloc;
		vk::SharedDevice impl;
		std::shared_ptr<vk::detail::DispatchLoaderDynamic> dispatcher;
		MemoryAllocator memory_allocator;
		/// Feature structs that actually got enabled; runtime code branches on these.
		std::unordered_set<vk::StructureType> enabled_features;
	};

	struct TimelineCompletion {
		vk::SharedSemaphore semaphore;
		std::uint64_t value;
	};

	struct BinaryCompletion {
		vk::SharedFence fence;
	};

	using CompletionPoint = std::variant<
		std::monostate,
		TimelineCompletion,
		BinaryCompletion
	>;

	class BinaryCompletionPointAllocator final : public std::enable_shared_from_this<BinaryCompletionPointAllocator> {
	public:
		struct ManagedBinaryCompletionPoint {
			std::shared_ptr<BinaryCompletionPointAllocator> owner;
			vk::SharedFence fence;
			std::vector<vk::SharedSemaphore> consumed_semaphores;
			bool submitted = false;

			ManagedBinaryCompletionPoint(
				std::shared_ptr<BinaryCompletionPointAllocator> const& owner,
				vk::SharedFence const& fence,
				std::vector<vk::SharedSemaphore>&& consumed_semaphores
			) noexcept
				: owner(owner),
				fence(fence),
				consumed_semaphores(std::move(consumed_semaphores)) {
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

		ManagedBinaryCompletionPoint AllocateCompletionPoint(
			std::size_t consumed_semaphore_count
		);
	};

	struct PlatformHandleHash {
		std::size_t operator()(execution::PlatformHandle const& value) const noexcept {
#if defined(__linux__)
			return std::visit(
				[](auto const& handle) {
					using Handle = std::remove_cvref_t<decltype(handle)>;
					std::size_t first;
					std::size_t second;
					if constexpr (std::same_as<Handle, execution::X11PlatformHandle>) {
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
			return std::hash<execution::PlatformHandle>{}(value);
#endif
		}
	};

	struct CommandSchedulerContext;
	struct CommandPoolContext;

	struct CompletionToken {
		struct ManagedCommandBuffer {
			std::shared_ptr<execution::CommandSchedulerContext> owner;
			CommandSchedulerContext* context;
			std::uint32_t family;
			vk::CommandBuffer impl;
			CompletionPoint completion;

			ManagedCommandBuffer(
				std::shared_ptr<execution::CommandSchedulerContext> const& owner,
				CommandSchedulerContext* context,
				std::uint32_t family,
				vk::CommandBuffer impl
			) noexcept
				: owner(owner),
				context(context),
				family(family),
				impl(impl) {
			}

			ManagedCommandBuffer(ManagedCommandBuffer const&) = delete;
			ManagedCommandBuffer& operator=(ManagedCommandBuffer const&) = delete;
			ManagedCommandBuffer(ManagedCommandBuffer&&) noexcept = default;
			ManagedCommandBuffer& operator=(ManagedCommandBuffer&&) noexcept = default;
			~ManagedCommandBuffer() noexcept;
		};

		std::shared_ptr<execution::CommandSchedulerContext> owner;
		CommandSchedulerContext* context;
		std::vector<vk::SharedRenderPass> render_passes;
		std::vector<vk::SharedFramebuffer> framebuffers;
		std::vector<vk::SharedSemaphore> presentation_semaphores;
		std::vector<vk::SharedFence> presentation_fences;
		std::vector<TimelineCompletion> timeline_completions;
		std::vector<BinaryCompletionPointAllocator::ManagedBinaryCompletionPoint> binary_completions;
		std::vector<ManagedCommandBuffer> command_buffers;
		std::vector<QueueWorkToken> work_tokens;
		std::exception_ptr exception;
		bool is_cancelled = false;

		explicit CompletionToken(
			std::shared_ptr<execution::CommandSchedulerContext> const& owner,
			CommandSchedulerContext* context
		) noexcept
			: owner(owner),
			context(context) {
		}

		CompletionToken(CompletionToken const&) = delete;
		CompletionToken& operator=(CompletionToken const&) = delete;

		CompletionToken(CompletionToken&& other) noexcept
			: owner(std::move(other.owner)),
			context(other.context),
			render_passes(std::move(other.render_passes)),
			framebuffers(std::move(other.framebuffers)),
			presentation_semaphores(std::move(other.presentation_semaphores)),
			presentation_fences(std::move(other.presentation_fences)),
			timeline_completions(std::move(other.timeline_completions)),
			binary_completions(std::move(other.binary_completions)),
			command_buffers(std::move(other.command_buffers)),
			work_tokens(std::move(other.work_tokens)),
			exception(std::move(other.exception)),
			is_cancelled(other.is_cancelled) {
		}

		~CompletionToken() noexcept;
	};

	struct CommandPoolContext {
		vk::SharedCommandPool impl;
		std::deque<vk::CommandBuffer> command_buffers;
		std::mutex mutex;
	};

	struct PresentationContext {
		struct BackBufferSynchronization {
			vk::SharedSemaphore acquire_semaphore;
			vk::SharedFence fence;
			std::uint64_t presentation_id;
		};

		struct SwapChain {
			vk::SharedSurfaceKHR surface;
			vk::SharedSwapchainKHR impl;
			std::vector<vk::Image> back_buffers;
			std::vector<BackBufferSynchronization> synchronization;
			std::size_t current_back_buffer_index;
			std::size_t last_back_buffer_index;
			std::uint64_t next_presentation_id;
			std::uint32_t present_family = vk::QueueFamilyIgnored;
			vk::Format format = vk::Format::eUndefined;
			vk::Extent2D extent{ 0u, 0u };
			std::uint32_t buffer_count = 0u;
			vk::PresentModeKHR present_mode = vk::PresentModeKHR::eFifo;
			bool out_of_date = false;
			bool swapchain_maintenance1_supported = false;
			bool fifo_latest_ready_supported = false;
			bool present_id_supported = false;
		};

		using List = plastic::ds::StaticList<
			std::pair<execution::PlatformHandle const, SwapChain>,
			32u
		>;
		using HashTable = plastic::ds::StaticHashTable<
			execution::PlatformHandle const,
			List::iterator,
			32u,
			PlatformHandleHash
		>;

		plastic::ds::LRUCache<HashTable, List, 32u> cache;
		std::mutex mutex;
	};

	struct CommandSchedulerContext {
		using TimelineMap = std::unordered_map<QueueIdentifier, TimelineCompletion>;

		vk::SharedPhysicalDevice physical_device;
		vk::SharedDevice device;
		std::shared_ptr<vk::detail::DispatchLoaderDynamic> dispatcher;
		QueueAllocator queue_allocator;
		std::unordered_map<std::uint32_t, CommandPoolContext> command_pools;
		std::mutex command_pools_mutex;
		std::variant<
			std::monostate,
			std::shared_ptr<BinaryCompletionPointAllocator>,
			TimelineMap
		> completion_points;
		std::mutex completion_points_mutex;
		std::unordered_map<std::uint32_t, PresentationContext> presentations;
		std::mutex presentations_mutex;
		bool timeline_semaphore_supported;
		bool synchronization2_supported;
		bool dynamic_rendering_supported;

		CommandSchedulerContext(
			vk::SharedPhysicalDevice const& physical_device,
			vk::SharedDevice const& device,
			std::shared_ptr<vk::detail::DispatchLoaderDynamic> const& dispatcher,
			QueueAllocator const& queue_allocator
		) noexcept
			: physical_device(physical_device),
			device(device),
			dispatcher(dispatcher),
			queue_allocator(queue_allocator),
			timeline_semaphore_supported(false),
			synchronization2_supported(false),
			dynamic_rendering_supported(false) {
		}

		CommandSchedulerContext(CommandSchedulerContext const&) = delete;
		CommandSchedulerContext& operator=(CommandSchedulerContext const&) = delete;

		CommandSchedulerContext(CommandSchedulerContext&& other) noexcept
			: physical_device(std::move(other.physical_device)),
			device(std::move(other.device)),
			dispatcher(std::move(other.dispatcher)),
			queue_allocator(std::move(other.queue_allocator)),
			command_pools(std::move(other.command_pools)),
			completion_points(std::move(other.completion_points)),
			presentations(std::move(other.presentations)),
			timeline_semaphore_supported(other.timeline_semaphore_supported),
			synchronization2_supported(other.synchronization2_supported),
			dynamic_rendering_supported(other.dynamic_rendering_supported) {
		}
	};

	struct Resource {
		std::shared_ptr<vk::detail::DispatchLoaderDynamic> dispatcher;
		ManagedAllocation allocation;
		std::uint32_t owner_family = vk::QueueFamilyIgnored;
		CompletionPoint completion;
	};

	struct View {
		std::variant<vk::SharedBufferView, vk::SharedImageView> impl;
		vk::Format format;
		vk::ImageSubresourceRange subresource_range;
	};

	struct Sampler {
		vk::SharedSampler impl;
	};

	struct Pipeline {
		std::shared_ptr<vk::detail::DispatchLoaderDynamic> dispatcher;
		vk::PipelineBindPoint bind_point = vk::PipelineBindPoint::eGraphics;
		std::vector<vk::SharedDescriptorSetLayout> descriptor_set_layouts;
		std::vector<pipeline::BindingMetadata> bindings;
		vk::SharedPipelineLayout layout;
		vk::SharedRenderPass compatible_render_pass;
		std::vector<vk::Format> color_formats;
		vk::Format depth_stencil_format = vk::Format::eUndefined;
		vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
		vk::SharedPipeline impl;
	};

	struct PipelineResourceGroup {
		std::uint32_t space;
		vk::SharedDescriptorPool pool;
		vk::DescriptorSet set;
		vk::SharedPipelineLayout layout;
	};

} // namespace fyuu_rhi::vulkan
#endif // !defined(__APPLE__)
