module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>

#include <chrono>

#include <thread>

#include <stop_token>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:completion_token_dispatch;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :execution;

namespace fyuu_rhi::execution {

	template <class NativeCompletionToken>
	struct PollCompletionToken {
		NativeCompletionToken* token;

		bool operator()() const noexcept {
			return true;
		}
	};

	template <class NativeCompletionToken>
	struct GetCompletionTokenError {
		NativeCompletionToken* token;

		std::exception_ptr operator()() const noexcept {
			return {};
		}
	};

	template <class NativeCompletionToken>
	struct IsCompletionTokenStopped {
		NativeCompletionToken* token;

		bool operator()() const noexcept {
			return false;
		}
	};

	/**
	 * @brief Blocks until a native token reaches a terminal state or a stop is requested.
	 *
	 * The return value has a strict two-way meaning, and callers depend on it:
	 *
	 *   true  -> the token is terminal (success, recorded error, or cancellation) and its
	 *            result is safe to read.
	 *   false -> @p stop_token was observed requested while the token was still
	 *            incomplete.
	 *
	 * A backend must therefore never report false merely because a bounded wait elapsed.
	 * Callers deliver the returned resources to the receiver on true, so treating an
	 * ordinary timeout as false would hand out resources the GPU may still be writing.
	 *
	 * Each backend owns its retry loop and chooses its own blocking granularity: a native
	 * fence/semaphore/event wait blocks until the GPU signals (bounded only so a stop is
	 * noticed promptly), while the fallback below must poll and therefore sleeps briefly.
	 * A shared quantum cannot serve both, so there is no such parameter.
	 */
	/**
	 * @brief Sleeps for one polling interval of the fallback wait below.
	 *
	 * Deliberately not inlined into the template: the template body is instantiated in
	 * whichever translation unit dispatches it, and those units do not include <chrono> in
	 * their global module fragment. Instantiating std::this_thread::sleep_for there fails to
	 * find std::chrono's time_point operators with this module toolchain, so the sleep
	 * machinery stays in this unit where the header is visible.
	 */
	void SleepBeforeCompletionPoll();

	template <class NativeCompletionToken>
	struct WaitCompletionToken {
		NativeCompletionToken* token;

		bool operator()(std::stop_token stop_token) const noexcept {
			// Portable fallback: no native wait is known for this backend, so poll at the
			// granularity the previous completion worker used.
			while (!PollCompletionToken<NativeCompletionToken>{ token }()) {
				if (stop_token.stop_requested()) {
					return false;
				}
				SleepBeforeCompletionPoll();
			}
			return true;
		}
	};

	void SleepBeforeCompletionPoll() {
		std::this_thread::sleep_for(std::chrono::milliseconds(1u));
	}

} // namespace fyuu_rhi::execution
