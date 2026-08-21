module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)
#define FYUU_RHI_USE_VULKAN_HEADER
#include <vulkan/vulkan_shared.hpp>
#endif // !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)

module fyuu_rhi:vulkan_completion_token;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
#if !defined(FYUU_RHI_USE_VULKAN_HEADER)
import vulkan;
#endif // !defined(FYUU_RHI_USE_VULKAN_HEADER)
import :completion_token_dispatch;
import :vulkan_data;

namespace fyuu_rhi::execution {

	template <>
	struct PollCompletionToken<vulkan::CompletionToken> {
		vulkan::CompletionToken* token;

		bool operator()() const noexcept {
			try {
				for (auto const& completion : token->timeline_completions) {
					auto value = token->context->device->getSemaphoreCounterValue(
						*completion.semaphore,
						*token->context->dispatcher
					);
					if (value < completion.value) {
						return false;
					}
				}
				for (auto const& completion : token->binary_completions) {
					auto result = completion.owner->device->getFenceStatus(
						*completion.fence,
						*completion.owner->dispatcher
					);
					if (result == vk::Result::eNotReady) {
						return false;
					}
				}
				for (auto const& fence : token->presentation_fences) {
					auto result = token->context->device->getFenceStatus(
						*fence,
						*token->context->dispatcher
					);
					if (result == vk::Result::eNotReady) {
						return false;
					}
				}
				return true;
			}
			catch (...) {
				if (!token->exception) {
					token->exception = std::current_exception();
				}
				return true;
			}
		}
	};

	template <>
	struct GetCompletionTokenError<vulkan::CompletionToken> {
		vulkan::CompletionToken* token;

		std::exception_ptr operator()() const noexcept {
			return token->exception;
		}
	};

	template <>
	struct IsCompletionTokenStopped<vulkan::CompletionToken> {
		vulkan::CompletionToken* token;

		bool operator()() const noexcept {
			return token->is_cancelled;
		}
	};

} // namespace fyuu_rhi::execution
#endif // !defined(__APPLE__)
