module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <utility>

#include <limits>

#include <mutex>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <d3d12.h>
#include <wrl.h>
#endif // defined(_WIN32)

module fyuu_rhi:d3d12_completion_token;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :completion_token_dispatch;
import :d3d12_data;
import :d3d12_utility;

namespace {
	constexpr std::uint64_t FailedFence = (std::numeric_limits<std::uint64_t>::max)();
}

namespace fyuu_rhi::d3d12 {
	QueueContext::ManagedCommandList&
	QueueContext::ManagedCommandList::operator=(
		ManagedCommandList&& other
	) noexcept {
		if (this == &other) {
			return *this;
		}
		ManagedCommandList previous(std::move(other));
		std::swap(owner, previous.owner);
		std::swap(impl, previous.impl);
		std::swap(fence_value, previous.fence_value);
		std::swap(is_open, previous.is_open);
		return *this;
	}

	QueueContext::ManagedCommandList::~ManagedCommandList() noexcept {
		try {
			if (!owner || !impl) {
				return;
			}
			if (
				fence_value != 0u &&
				owner->fence->GetCompletedValue() < fence_value
			) {
				WaitForFence(
					owner->fence,
					fence_value
				);
			}
			if (is_open) {
				(void)impl->Close();
			}
			std::unique_lock<std::mutex> lock(owner->command_lists_mutex);
			owner->command_lists.emplace_back(std::move(impl));
		}
		catch (...) {
		}
	}

}

namespace fyuu_rhi::execution {
	template <>
	struct PollCompletionToken<d3d12::CompletionToken> {
		d3d12::CompletionToken* token;

		bool operator()() const noexcept {
			for (auto& commands : token->command_lists) {
				if (
					!commands.impl ||
					!commands.owner ||
					commands.fence_value == 0u
				) {
					continue;
				}
				auto completed = commands.owner->fence->GetCompletedValue();
				if (completed == FailedFence) {
					try {
						auto device = d3d12::GetLogicalDevice(commands.owner->impl);
						d3d12::ThrowIfFailed(device->GetDeviceRemovedReason());
					}
					catch (...) {
						if (!token->exception) {
							token->exception = std::current_exception();
						}
					}
					commands.owner.reset();
					return true;
				}
				if (completed < commands.fence_value) {
					return false;
				}
			}
			return true;
		}
	};

	template <>
	struct GetCompletionTokenError<d3d12::CompletionToken> {
		d3d12::CompletionToken* token;

		std::exception_ptr operator()() const noexcept {
			return token->exception;
		}
	};

	template <>
	struct IsCompletionTokenStopped<d3d12::CompletionToken> {
		d3d12::CompletionToken* token;

		bool operator()() const noexcept {
			return token->is_cancelled;
		}
	};
}
#endif // defined(_WIN32)
