module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
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

} // namespace fyuu_rhi::execution
