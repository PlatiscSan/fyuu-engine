module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <atomic>
#include <thread>

#include <stop_token>
#endif // !defined(__cpp_lib_modules)

module fyuu_asset;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :operation;

namespace fyuu_asset {
	void ScheduleAssetTask(OperationTask* submitted) {
		static std::atomic<OperationTask*> tasks = nullptr;
		static std::atomic_size_t notification = 0u;
		static std::jthread asset_thread([](std::stop_token token) noexcept {
			std::stop_callback WakeWorker(token, []() noexcept {
				notification.fetch_add(1u, std::memory_order_release);
				notification.notify_one();
			});

			while (!token.stop_requested() || tasks.load(std::memory_order_acquire)) {
				auto* submitted = tasks.exchange(nullptr, std::memory_order_acquire);
				OperationTask* first = nullptr;
				while (submitted) {
					auto* next = submitted->m_next.load(std::memory_order_relaxed);
					submitted->m_next.store(first, std::memory_order_relaxed);
					first = submitted;
					submitted = next;
				}

				while (first) {
					auto* current = first;
					first = first->m_next.load(std::memory_order_relaxed);
					auto const continuation = current->m_continuation;
					current->Execute(current);
					if (continuation) {
						continuation.resume();
					}
				}

				auto const observed = notification.load(std::memory_order_acquire);
				if (!token.stop_requested() && !tasks.load(std::memory_order_acquire)) {
					notification.wait(observed, std::memory_order_acquire);
				}
			}
		});

		auto* first = tasks.load(std::memory_order_relaxed);
		do {
			submitted->m_next.store(first, std::memory_order_relaxed);
		} while (!tasks.compare_exchange_weak(
		    first,
		    submitted,
		    std::memory_order_release,
		    std::memory_order_relaxed
		));
		notification.fetch_add(1u, std::memory_order_release);
		notification.notify_one();
	}
} // namespace fyuu_asset
