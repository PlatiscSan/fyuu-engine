module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <cstdint>

#include <variant>

#include <stop_token>
#include <string>
#include <string_view>
#endif // !defined(__cpp_lib_modules)
#include <dawn/webgpu_cpp.h>

module fyuu_rhi:webgpu_data;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :execution;
import :pipeline;

namespace fyuu_rhi::webgpu {

	struct Instance {
		wgpu::Instance impl;
	};

	struct PhysicalDevice {
		wgpu::Instance instance;
		wgpu::Adapter adapter;
	};

	/// One device's first reported failure.
	///
	/// Dawn delivers a validation error only to the device's uncaptured-error callback, and
	/// neither the queue-work future nor a buffer mapping ever completes because of it, so the
	/// first failure is latched here: the submit that produced it fails the completion token,
	/// and a mapping that will never complete reports it instead of waiting forever.
	struct DeviceError {
		std::atomic_bool failed = false;
		/// Mutable because Message() reads the recorded text from a const device or resource.
		mutable std::mutex mutex;
		std::string message;

		/// Records the first failure. Runs inside a Dawn callback, so it must not throw.
		void Fail(std::string_view text) noexcept {
			try {
				bool expected = false;
				if (!failed.compare_exchange_strong(expected, true)) {
					return;
				}
				std::lock_guard lock(mutex);
				message.assign(text);
			}
			catch (...) {
			}
		}

		[[nodiscard]] bool Failed() const noexcept {
			return failed.load(std::memory_order_acquire);
		}

		[[nodiscard]] std::string Message() const {
			std::lock_guard lock(mutex);
			return message;
		}
	};

	struct LogicalDevice {
		wgpu::Instance instance;
		wgpu::Adapter adapter;
		/// Declared before the device so the latch it is handed to as user data outlives the
		/// device: members are destroyed in reverse order.
		std::shared_ptr<DeviceError> errors;
		wgpu::Device impl;
	};

	struct CompletionState {
		std::atomic_bool complete = false;
		std::atomic_bool stopped = false;
		std::mutex mutex;
		/// Wakes the completion wait. The scheduler's pump thread owns completion, so a
		/// waiting thread cannot drive the Dawn future itself and must be notified instead.
		std::condition_variable condition;
		std::exception_ptr error;
	};

	struct CompletionToken {
		wgpu::Instance instance;
		std::shared_ptr<CompletionState> state;
		wgpu::Future future;
	};

	struct CommandSchedulerContext {
		struct SurfaceState {
			execution::PlatformHandle handle;
			wgpu::Surface surface;
			std::uint32_t width = 0u;
			std::uint32_t height = 0u;
			wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
		};

		/// One Dawn future the pump thread waits on, together with the completion state
		/// observing it completes. The state is shared rather than owned so the pump can
		/// finish a token whose caller is already gone, or one published after the pump
		/// finished another round.
		struct WatchedFuture {
			wgpu::Future future;
			std::shared_ptr<CompletionState> state;
		};

		wgpu::Instance instance;
		wgpu::Device device;
		std::shared_ptr<DeviceError> errors;
		std::vector<SurfaceState> surfaces;
		std::mutex surfaces_mutex;

		/// Outstanding completion futures, published by `Watch` and consumed by the pump
		/// thread. The pump copies this list out instead of holding the lock across a wait.
		std::mutex pump_mutex;
		/// Bounds the pump's sleep between rounds and lets the destructor wake it, since
		/// jthread::request_stop does not wake a condition variable on its own.
		std::condition_variable pump_condition;
		std::vector<WatchedFuture> pump_futures;
		/// Set by the pump thread once Dawn reported the device lost, so every later
		/// future is completed as an error instead of being waited on forever.
		bool pump_device_lost = false;
		std::exception_ptr pump_device_lost_error;
		/// Set by the pump thread once it has published its state, so the move constructor
		/// does not return before the thread is running against this object.
		std::atomic_bool context_ready = false;
		/// The completion pump. Declared last so it is joined while the instance and device
		/// handles it waits on are still alive.
		std::jthread pump_thread;

		CommandSchedulerContext(
			wgpu::Instance const& instance,
			wgpu::Device const& device,
			std::shared_ptr<DeviceError> errors
		) noexcept
			: instance(instance),
			device(device),
			errors(std::move(errors)) {
		}

		CommandSchedulerContext(CommandSchedulerContext const&) = delete;
		CommandSchedulerContext& operator=(CommandSchedulerContext const&) = delete;

		CommandSchedulerContext(CommandSchedulerContext&& other) noexcept;
		~CommandSchedulerContext() noexcept;

		/// Publishes one outstanding Dawn future to the pump thread.
		void Watch(wgpu::Future future, std::shared_ptr<CompletionState> const& state);

		/// The pump thread: waits on every published future plus the device-loss future
		/// with a zero timeout, and publishes each completion it observes.
		void Run(std::stop_token stop_token);
	};

	struct Resource {
		std::variant<wgpu::Buffer, wgpu::Texture> impl;
		/// The owning device's failure latch, so Map() can fail instead of blocking on a
		/// device that already reported that the work referencing this resource was rejected.
		std::shared_ptr<DeviceError> errors;
	};

	struct View {
		struct Buffer {
			wgpu::Buffer impl;
			std::size_t offset;
			std::size_t size;
		};

		std::variant<Buffer, wgpu::TextureView> impl;
	};

	struct Sampler {
		wgpu::Sampler impl;
	};

	struct Pipeline {
		struct ConstantRange {
			std::uint32_t abi_slot;
			std::uint32_t abi_space;
			std::uint32_t offset;
			std::uint32_t size;
			std::uint32_t binding;
		};

		wgpu::Device device;
		std::vector<wgpu::BindGroupLayout> bind_group_layouts;
		std::vector<pipeline::BindingMetadata> bindings;
		std::vector<ConstantRange> constant_ranges;
		std::uint32_t constant_group;
		bool native_immediates;
		std::variant<
			std::monostate,
			wgpu::RenderPipeline,
			wgpu::ComputePipeline
		> impl;
	};

	struct PipelineResourceGroup {
		struct DynamicBuffer {
			std::size_t entry;
			std::uint64_t base_offset;
			std::uint64_t size;
			std::uint64_t capacity;
		};

		std::uint32_t space;
		wgpu::Device device;
		wgpu::BindGroupLayout layout;
		std::vector<wgpu::BindGroupEntry> entries;
		std::vector<DynamicBuffer> dynamic_buffers;
		wgpu::BindGroup impl;
	};

} // namespace fyuu_rhi::webgpu::data
