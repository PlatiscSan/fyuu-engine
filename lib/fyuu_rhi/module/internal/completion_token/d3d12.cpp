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
		std::swap(descriptors, previous.descriptors);
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

	template <>
	struct WaitCompletionToken<d3d12::CompletionToken> {
		d3d12::CompletionToken* token;

		/// Longest one fence wait blocks. It only bounds how promptly a stop request is
		/// noticed; a fence that reaches its value sooner releases the wait immediately.
		static constexpr std::uint32_t WaitTimeoutMilliseconds = 50u;

		bool operator()(std::stop_token stop_token) const noexcept {
			// The loop lives here rather than at the call site so that this returns false
			// only for a stop request: a bounded wait that merely elapsed must never be
			// reported as incomplete, because the caller reads false as "abandon, do not
			// hand the bindings to the receiver".
			for (;;) {
				if (PollCompletionToken<d3d12::CompletionToken>{ token }()) {
					return true;
				}
				if (stop_token.stop_requested()) {
					return false;
				}
				// One token can span several queues, so a single reached fence is not enough
				// to report completion: block on the first entry Poll() still considers
				// outstanding and let the next iteration re-decide. Poll() skips the same
				// entries skipped here, so at least one wait always happens and this cannot
				// spin.
				for (auto const& commands : token->command_lists) {
					if (
						!commands.impl ||
						!commands.owner ||
						commands.fence_value == 0u
					) {
						continue;
					}
					if (commands.owner->fence->GetCompletedValue() >= commands.fence_value) {
						continue;
					}
					try {
						// Unlike the destructor path this must never wait forever, so the
						// event timeout is finite.
						(void)fyuu_rhi::d3d12::WaitForFenceFor(
							commands.owner->fence,
							commands.fence_value,
							WaitTimeoutMilliseconds
						);
					}
					catch (...) {
						// SetEventOnCompletion fails once the device is gone; Poll() below
						// records the removal reason and reports terminal.
					}
					break;
				}
			}
		}
	};
}
#endif // defined(_WIN32)
