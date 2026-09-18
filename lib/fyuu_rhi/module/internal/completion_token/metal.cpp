module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <format>
#include <stdexcept>
#include <string>

#include <chrono>

#include <atomic>
#include <condition_variable>
#include <mutex>

#include <stop_token>
#endif // !defined(__cpp_lib_modules)
#if defined(__APPLE__)
#include <Metal/Metal.hpp>
#endif // defined(__APPLE__)

module fyuu_rhi:metal_completion_token;
#if defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :completion_token_dispatch;
import :metal_data;

namespace fyuu_rhi::execution {
	namespace {
		std::string ErrorMessage(NS::Error* error) {
			if (!error || !error->localizedDescription()) {
				return "No Metal diagnostic message";
			}
			return error->localizedDescription()->utf8String();
		}

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

	// NOTE: Metal is guarded by __APPLE__ and cannot be built or run on the machine this
	// change was written on, so everything below is unverified against a real Metal device.
	// The shape matches the OpenGL and WebGPU paths, which are verified.

	template <>
	struct PollCompletionToken<metal::CompletionToken> {
		metal::CompletionToken* token;

		bool operator()() const noexcept {
			if (!token->state) {
				return true;
			}
			bool complete = true;
			for (auto const& command_buffer : token->command_buffers) {
				auto status = command_buffer->status();
				if (status == MTL::CommandBufferStatusError) {
					std::unique_lock<std::mutex> state_lock(token->state->mutex);
					if (!token->state->error) {
						try {
							token->state->error = std::make_exception_ptr(
								std::runtime_error(
									std::format(
										"Metal command buffer failed: {}",
										ErrorMessage(command_buffer->error())
									)
								)
							);
						}
						catch (...) {
							token->state->error = std::current_exception();
						}
					}
					continue;
				}
				if (status != MTL::CommandBufferStatusCompleted) {
					complete = false;
				}
			}
			return complete;
		}
	};

	template <>
	struct GetCompletionTokenError<metal::CompletionToken> {
		metal::CompletionToken* token;

		std::exception_ptr operator()() const noexcept {
			if (!token->state) {
				return {};
			}
			std::unique_lock<std::mutex> state_lock(token->state->mutex);
			return token->state->error;
		}
	};

	template <>
	struct IsCompletionTokenStopped<metal::CompletionToken> {
		metal::CompletionToken* token;

		bool operator()() const noexcept {
			return token->state &&
				token->state->stopped.load(std::memory_order_acquire);
		}
	};

	template <>
	struct WaitCompletionToken<metal::CompletionToken> {
		metal::CompletionToken* token;

		bool operator()(std::stop_token stop_token) const noexcept {
			if (!token->state) {
				return true;
			}
			// Every command buffer's completed handler publishes into this state, so the
			// wait is event-driven rather than a status poll. Completion is published under
			// this mutex and notified after the release, so re-testing the flag while
			// holding the lock cannot miss a wake-up.
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
#endif // defined(__APPLE__)
