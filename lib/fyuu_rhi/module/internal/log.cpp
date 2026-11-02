module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>

#include <limits>

#include <atomic>

#include <string_view>
#include <source_location>
#endif // !defined(__cpp_lib_modules)
module fyuu_rhi:log;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core;

namespace {
	/** @brief Admission and retirement share one atomic word, closing the check/enter race. */
	constexpr std::size_t SinkClosed = std::size_t{1} << (std::numeric_limits<std::size_t>::digits - 1u);
	/// @brief Marks a rebind owner; other rebinders wait without touching its pointer.
	constexpr std::size_t SinkChanging = SinkClosed >> 1u;
	/// @brief Remaining bits hold the number of calls borrowing the current Sink.
	constexpr std::size_t SinkCallMask = SinkChanging - 1u;

	/** @brief Counts admitted calls below the two state bits; the pointer is borrowed. */
	struct SinkState {
		std::atomic<fyuu_rhi::log::Sink*> sink = nullptr;
		std::atomic_size_t calls = SinkClosed;
	};

	/** @brief Owns the process-wide logging registration without a separate allocation. */
	SinkState& CurrentSink() noexcept {
		static SinkState state;
		return state;
	}

	/** @brief Keeps an admitted Sink call alive until its Write invocation returns. */
	struct SinkCall {
		fyuu_rhi::log::Sink* sink;

		static fyuu_rhi::log::Sink* Acquire() noexcept {
			auto& state = CurrentSink();
			auto calls = state.calls.load(std::memory_order_relaxed);
			while ((calls & SinkClosed) == 0u && calls != SinkCallMask) {
				if (state.calls.compare_exchange_weak(
					calls, calls + 1u,
					std::memory_order_acquire, std::memory_order_relaxed
				)) {
					return state.sink.load(std::memory_order_relaxed);
				}
			}
			return nullptr;
		}

		SinkCall() noexcept : sink(Acquire()) {
		}

		SinkCall(SinkCall const&) = delete;
		SinkCall& operator=(SinkCall const&) = delete;

		~SinkCall() noexcept {
			if (sink) {
				auto& state = CurrentSink();
				if ((state.calls.fetch_sub(1u, std::memory_order_release) & SinkCallMask) == 1u) {
					state.calls.notify_all();
				}
			}
		}
	};
}

namespace fyuu_rhi::log {

	void SetSink(Sink* sink) noexcept {
		auto& state = CurrentSink();
		/// @brief Atomically claim the rebind and close admission, preserving existing borrowers.
		auto calls = state.calls.load(std::memory_order_acquire);
		while (true) {
			if ((calls & SinkChanging) != 0u) {
				state.calls.wait(calls, std::memory_order_acquire);
				calls = state.calls.load(std::memory_order_acquire);
			}
			else if (state.calls.compare_exchange_weak(
				calls, calls | SinkClosed | SinkChanging,
				std::memory_order_acq_rel, std::memory_order_acquire
			)) {
				break;
			}
		}
		calls = state.calls.load(std::memory_order_acquire);
		while ((calls & SinkCallMask) != 0u) {
			state.calls.wait(calls, std::memory_order_acquire);
			calls = state.calls.load(std::memory_order_acquire);
		}
		/// @brief Publish only after all old writes return, then admit callers to the new Sink.
		state.sink.store(sink, std::memory_order_relaxed);
		state.calls.store(sink ? 0u : SinkClosed, std::memory_order_release);
		state.calls.notify_all();
	}

	void Debug(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept {
		SinkCall call;
		if (call.sink) {
			call.sink->Write(Level::Debug, message, location);
		}
	}

	void Info(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept {
		SinkCall call;
		if (call.sink) {
			call.sink->Write(Level::Info, message, location);
		}
	}

	void Warning(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept {
		SinkCall call;
		if (call.sink) {
			call.sink->Write(Level::Warning, message, location);
		}
	}

	void Error(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept {
		SinkCall call;
		if (call.sink) {
			call.sink->Write(Level::Error, message, location);
		}
	}

	void Fatal(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept {
		SinkCall call;
		if (call.sink) {
			call.sink->Write(Level::Fatal, message, location);
		}
	}

} // namespace fyuu_rhi::log
