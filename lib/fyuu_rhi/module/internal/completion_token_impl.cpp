module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>

#include <deque>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <variant>

#include <stop_token>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :completion_token_dispatch;
import :completion_token_factory;
import :execution;
import :webgpu_completion_token;
#if defined(__APPLE__)
import :metal_completion_token;
#endif // defined(__APPLE__)
#if defined(_WIN32)
import :d3d12_completion_token;
#endif // defined(_WIN32)
#if !defined(__APPLE__)
import :opengl_completion_token;
import :vulkan_completion_token;
#endif // !defined(__APPLE__)

namespace fyuu_rhi::execution {

	bool CompletionToken::Poll() noexcept {
		if (!m_impl) {
			return true;
		}
		return std::visit(
		    []<class NativeCompletionToken>(NativeCompletionToken& native) noexcept {
			    return PollCompletionToken<NativeCompletionToken>{&native}();
		    },
		    m_impl->native
		);
	}

	std::exception_ptr CompletionToken::Error() noexcept {
		if (!m_impl) {
			return {};
		}
		return std::visit(
		    []<class NativeCompletionToken>(NativeCompletionToken& native) noexcept {
			    return GetCompletionTokenError<NativeCompletionToken>{&native}();
		    },
		    m_impl->native
		);
	}

	bool CompletionToken::IsStopped() noexcept {
		if (!m_impl) {
			return false;
		}
		return std::visit(
		    []<class NativeCompletionToken>(NativeCompletionToken& native) noexcept {
			    return IsCompletionTokenStopped<NativeCompletionToken>{&native}();
		    },
		    m_impl->native
		);
	}

	bool CompletionToken::Wait(std::stop_token stop_token) noexcept {
		if (!m_impl) {
			return true;
		}
		return std::visit(
		    [stop_token]<class NativeCompletionToken>(NativeCompletionToken& native) noexcept {
			    return WaitCompletionToken<NativeCompletionToken>{&native}(stop_token);
		    },
		    m_impl->native
		);
	}

	namespace details {

		namespace {

			/**
			 * @brief Completion driver state, owned by the stack frame of RunCompletionDriver.
			 *
			 * One driver thread serves the process. It runs each task's native wait and then that
			 * task's receiver, so there is no worker pool and nothing to size: the delivering side
			 * is deliberately serial, and the state lives on the driver's own frame rather than in
			 * a heap object, published through an atomic.
			 */
			struct DriverState {
				std::mutex mutex;
				/// Stop-token-aware, so teardown of the owning jthread wakes the driver without a
				/// polling timer and without a separate stop_source.
				std::condition_variable_any condition;
				std::deque<CompletionTask> tasks;
				/// Notified by the driver once every queued task has been delivered.
				std::condition_variable drained_condition;
				bool shutting_down = false;
				bool drained = false;
			};

			/// Published once the driver's frame is ready; cleared only after it has finished.
			/// Its own non-null value is the readiness signal, so no separate flag is needed.
			std::atomic<DriverState*> s_driver{nullptr};

			/// The driver thread. Joinable for the process lifetime.
			std::jthread s_driver_thread;

			void RunCompletionDriver(std::stop_token stop) {
				DriverState state;
				s_driver.store(&state, std::memory_order_release);
				s_driver.notify_all();

				for (;;) {
					CompletionTask task;
					{
						std::unique_lock lock(state.mutex);
						state.condition.wait(lock, stop, [&state]() {
							return !state.tasks.empty() || state.shutting_down;
						});
						if (state.tasks.empty()) {
							// Only reachable while shutting down or once this thread's own stop was
							// requested, with nothing left to deliver.
							break;
						}
						task = std::move(state.tasks.front());
						state.tasks.pop_front();
					}
					// Blocks in the backend's native wait and then delivers exactly one terminal
					// signal; a requested stop makes the task report cancellation instead.
					task(stop);
				}

				{
					std::unique_lock lock(state.mutex);
					state.drained = true;
					state.drained_condition.notify_all();
					// Park until this thread is torn down at process exit, rather than returning.
					// Holding the frame keeps the published state valid for the rest of the
					// process, so a submitter arriving after shutdown locks a live object instead
					// of a dangling pointer. The stop-aware wait costs nothing while parked and is
					// what lets the owning jthread's destructor join this thread at exit.
					//
					// The stop request must not be raised on the explicit shutdown path: once it
					// is, every stop-aware wait below returns immediately and this park would
					// become a no-op, destroying the state under the shutdown caller.
					state.condition.wait(
						lock,
						stop,
						[]() {
							return false;
						}
					);
				}
				s_driver.store(nullptr, std::memory_order_release);
			}

			/// Returns the driver's state, starting the driver on first use; null once stopped.
			DriverState* AcquireDriver() {
				// One-time start. The driver publishes its own frame, so waiting for the pointer to
				// become non-null is the whole handshake; other callers block until that happens.
				static const bool started = []() {
					s_driver_thread = std::jthread([](std::stop_token stop) {
						RunCompletionDriver(stop);
					});
					s_driver.wait(nullptr, std::memory_order_acquire);
					return true;
				}();
				(void)started;
				return s_driver.load(std::memory_order_acquire);
			}

		} // namespace

		void EnqueueCompletionTask(CompletionTask&& task) {
			DriverState* driver = AcquireDriver();
			if (!driver) {
				// The driver has stopped, so deliver on this thread rather than leave the
				// operation without a terminal signal. A default stop token never reports a stop,
				// so this waits for the real result instead of a cancellation.
				task(std::stop_token{});
				return;
			}
			{
				std::unique_lock lock(driver->mutex);
				if (driver->shutting_down) {
					lock.unlock();
					task(std::stop_token{});
					return;
				}
				driver->tasks.emplace_back(std::move(task));
			}
			driver->condition.notify_one();
		}

		void ShutdownCompletionDriver() {
			DriverState* driver = AcquireDriver();
			if (!driver) {
				return;
			}
			{
				std::unique_lock lock(driver->mutex);
				if (driver->shutting_down) {
					return;
				}
				driver->shutting_down = true;
			}
			driver->condition.notify_all();
			// The driver keeps running the queue, so every operation still in flight reaches a
			// terminal state here. Its work is already submitted, so this waits for the real
			// result rather than reporting a cancellation the GPU cannot honour.
			std::unique_lock lock(driver->mutex);
			driver->drained_condition.wait(lock, [driver]() {
				return driver->drained;
			});
		}

	} // namespace details

} // namespace fyuu_rhi::execution
