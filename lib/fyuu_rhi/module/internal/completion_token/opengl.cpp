module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <memory>
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

} // namespace fyuu_rhi::execution
#endif // !defined(__APPLE__)
