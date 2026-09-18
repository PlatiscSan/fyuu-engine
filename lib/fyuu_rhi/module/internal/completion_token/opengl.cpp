module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <memory>

#include <chrono>

#include <condition_variable>
#include <mutex>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:opengl_completion_token;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :completion_token_dispatch;
import :opengl_data;

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
	struct PollCompletionToken<opengl::CompletionToken> {
		opengl::CompletionToken* token;

		bool operator()() const noexcept {
			return !token->state ||
				token->state->complete.load(std::memory_order_acquire);
		}
	};

	template <>
	struct GetCompletionTokenError<opengl::CompletionToken> {
		opengl::CompletionToken* token;

		std::exception_ptr operator()() const noexcept {
			if (!token->state) {
				return {};
			}
			std::unique_lock<std::mutex> state_lock(token->state->mutex);
			return token->state->error;
		}
	};

	template <>
	struct IsCompletionTokenStopped<opengl::CompletionToken> {
		opengl::CompletionToken* token;

		bool operator()() const noexcept {
			return token->state &&
				token->state->stopped.load(std::memory_order_acquire);
		}
	};

	template <>
	struct WaitCompletionToken<opengl::CompletionToken> {
		opengl::CompletionToken* token;

		bool operator()(std::stop_token stop_token) const noexcept {
			if (!token->state) {
				return true;
			}
			// This thread cannot poll: reaping the GL fence requires the GL context, which
			// only the scheduler's GL thread owns. The state's own condition variable is what
			// makes the wait event-driven instead. Completion is published under this mutex
			// and notified after the release, so re-testing the flag while holding the lock
			// cannot miss a wake-up.
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
#endif // !defined(__APPLE__)
