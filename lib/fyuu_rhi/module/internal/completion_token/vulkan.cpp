module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>

#include <vector>

#include <cstdint>

#include <stop_token>
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

	template <>
	struct WaitCompletionToken<vulkan::CompletionToken> {
		vulkan::CompletionToken* token;

		/// Longest one native wait blocks. It only bounds how promptly a stop request is
		/// noticed; a token that finishes sooner is observed immediately.
		static constexpr std::uint64_t WaitTimeoutNanoseconds = 50'000'000u;

		bool operator()(std::stop_token stop_token) const noexcept {
			// The loop lives here rather than at the call site so that this returns false
			// only for a stop request: a bounded wait that simply elapsed must never be
			// reported as incomplete, because the caller reads false as "abandon, do not
			// hand the bindings to the receiver".
			for (;;) {
				if (PollCompletionToken<vulkan::CompletionToken>{ token }()) {
					return true;
				}
				if (stop_token.stop_requested()) {
					return false;
				}
				try {
					// Timeline semaphores wait on every listed (semaphore, value) pair and
					// fences on all of them, so one round covers whatever is still
					// outstanding; Poll() remains the single source of truth for the
					// combination of timelines, binary completions and presentation fences.
					std::vector<vk::Semaphore> semaphores;
					std::vector<std::uint64_t> values;
					semaphores.reserve(token->timeline_completions.size());
					values.reserve(token->timeline_completions.size());
					for (auto const& completion : token->timeline_completions) {
						semaphores.emplace_back(*completion.semaphore);
						values.emplace_back(completion.value);
					}
					std::vector<vk::Fence> fences;
					fences.reserve(
						token->binary_completions.size() + token->presentation_fences.size()
					);
					for (auto const& completion : token->binary_completions) {
						fences.emplace_back(*completion.fence);
					}
					for (auto const& fence : token->presentation_fences) {
						fences.emplace_back(*fence);
					}
					if (!semaphores.empty()) {
						vk::SemaphoreWaitInfo wait_info(
							vk::SemaphoreWaitFlags{},
							static_cast<std::uint32_t>(semaphores.size()),
							semaphores.data(),
							values.data()
						);
						(void)token->context->device->waitSemaphores(
							wait_info,
							WaitTimeoutNanoseconds,
							*token->context->dispatcher
						);
					}
					if (!fences.empty()) {
						(void)token->context->device->waitForFences(
							fences,
							true,
							WaitTimeoutNanoseconds,
							*token->context->dispatcher
						);
					}
				}
				catch (...) {
					// A lost device throws here. Poll() records the same exception the
					// polling path would have recorded and then reports terminal, so the
					// next iteration returns it to the caller.
				}
			}
		}
	};

} // namespace fyuu_rhi::execution
#endif // !defined(__APPLE__)
