module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <mutex>

#include <atomic>
#endif // !defined(__cpp_lib_modules)
#include <dawn/webgpu_cpp.h>

module fyuu_rhi:webgpu_completion_token;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :completion_token_dispatch;
import :webgpu_data;

namespace fyuu_rhi::execution {

	template <>
	struct PollCompletionToken<webgpu::CompletionToken> {
		webgpu::CompletionToken* token;

		bool operator()() const noexcept {
			if (!token->state) {
				return true;
			}
			token->instance.ProcessEvents();
			return token->state->complete.load(std::memory_order_acquire);
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

} // namespace fyuu_rhi::execution
