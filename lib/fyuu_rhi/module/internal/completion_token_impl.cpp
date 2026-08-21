module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <variant>
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
			[]<class NativeCompletionToken>(NativeCompletionToken& native) noexcept
			{
				return PollCompletionToken<NativeCompletionToken>{ &native }();
			},
			m_impl->native
		);
	}

	std::exception_ptr CompletionToken::Error() noexcept {
		if (!m_impl) {
			return {};
		}
		return std::visit(
			[]<class NativeCompletionToken>(NativeCompletionToken& native) noexcept
			{
				return GetCompletionTokenError<NativeCompletionToken>{ &native }();
			},
			m_impl->native
		);
	}

	bool CompletionToken::IsStopped() noexcept {
		if (!m_impl) {
			return false;
		}
		return std::visit(
			[]<class NativeCompletionToken>(NativeCompletionToken& native) noexcept
			{
				return IsCompletionTokenStopped<NativeCompletionToken>{ &native }();
			},
			m_impl->native
		);
	}

} // namespace fyuu_rhi::execution
