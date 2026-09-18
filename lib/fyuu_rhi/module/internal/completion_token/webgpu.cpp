module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>

#include <chrono>

#include <atomic>
#include <condition_variable>
#include <mutex>

#include <stop_token>
#endif // !defined(__cpp_lib_modules)
#include <dawn/webgpu_cpp.h>

module fyuu_rhi:webgpu_completion_token;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :completion_token_dispatch;
import :webgpu_data;

namespace fyuu_rhi::execution {

	namespace {
		/// Blocks up to the timeout for one completion broadcast.
		///
		/// Deliberately a plain function rather than a call inside the specialization below:
		/// a specialization body is instantiated wherever it is dispatched, and those units do
		/// not include <chrono> in their global module fragment, so instantiating the
		/// condition_variable's time_point arithmetic there fails with this toolchain. The
		/// caller re-tests its own predicate after each return, so a spurious wake-up is
		/// harmless.
		void WaitForCompletionBroadcast(
			std::condition_variable& condition,
			std::unique_lock<std::mutex>& lock
		) {
			(void)condition.wait_for(lock, std::chrono::milliseconds(50u));
		}
	}

	template <>
	struct PollCompletionToken<webgpu::CompletionToken> {
		webgpu::CompletionToken* token;

		bool operator()() const noexcept {
			// The scheduler's pump thread owns the Dawn future and drives Dawn's callback
			// processing, so a poll never waits: it only reports what the pump published.
			return !token->state ||
				token->state->complete.load(std::memory_order_acquire);
		}
	};

	template <>
	struct GetCompletionTokenError<webgpu::CompletionToken> {
		webgpu::CompletionToken* token;

		std::exception_ptr operator()() const noexcept {
			if (!token->state) {
				return {};
			}
			std::unique_lock<std::mutex> state_lock(token->state->mutex);
			return token->state->error;
		}
	};

	template <>
	struct IsCompletionTokenStopped<webgpu::CompletionToken> {
		webgpu::CompletionToken* token;

		bool operator()() const noexcept {
			return token->state &&
				token->state->stopped.load(std::memory_order_acquire);
		}
	};

	template <>
	struct WaitCompletionToken<webgpu::CompletionToken> {
		webgpu::CompletionToken* token;

		bool operator()(std::stop_token stop_token) const noexcept {
			if (!token->state) {
				return true;
			}
			// This thread must not poll: driving the Dawn future needs WaitAny, and the
			// only timeout that is safe to wait on (zero) needs to be called repeatedly,
			// which the scheduler's pump thread already does for every outstanding
			// future. Completion is published under this mutex and notified after the
			// release, so re-testing the flag while holding the lock cannot miss a
			// wake-up.
			std::unique_lock lock(token->state->mutex);
			while (!token->state->complete.load(std::memory_order_acquire)) {
				if (stop_token.stop_requested()) {
					return false;
				}
				WaitForCompletionBroadcast(token->state->condition, lock);
			}
			return true;
		}
	};

} // namespace fyuu_rhi::execution
