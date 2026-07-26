module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#endif // !defined(__cpp_lib_modules)
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

module fyuu_rhi:completion_service;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace fyuu_rhi::execution {

	class CompletionService : public boost::intrusive_ref_counter<
		CompletionService,
		boost::thread_safe_counter
	> {
	private:
		struct Task {
			std::function<bool()> Poll;
			std::function<void()> Complete;
		};

		std::atomic<std::deque<Task>*> m_tasks = nullptr;
		std::atomic<std::mutex*> m_mutex = nullptr;
		std::atomic<std::condition_variable*> m_condition = nullptr;
		std::jthread m_worker;

		static void RunWorker(
			std::stop_token stop_token,
			CompletionService* service
		) {
			service->Run(stop_token);
		}

		void Run(std::stop_token stop_token) {
			std::deque<Task> tasks;
			std::deque<Task> current;
			std::deque<Task> pending;
			std::mutex mutex;
			std::condition_variable condition;

			m_mutex.store(&mutex, std::memory_order::relaxed);
			m_condition.store(&condition, std::memory_order::relaxed);
			m_tasks.store(&tasks, std::memory_order::release);
			m_tasks.notify_one();

			while (!stop_token.stop_requested()) {
				{
					std::unique_lock<std::mutex> lock(mutex);
					auto HasTasksOrStopped = [&tasks, stop_token]() noexcept {
						return stop_token.stop_requested() || !tasks.empty();
					};
					condition.wait_for(
						lock,
						std::chrono::milliseconds(1u),
						HasTasksOrStopped
					);
					current.swap(tasks);
				}
				if (stop_token.stop_requested()) {
					break;
				}

				while (!current.empty()) {
					auto task = std::move(current.front());
					current.pop_front();
					bool completed = task.Poll();
					if (completed) {
						task.Complete();
					}
					else {
						pending.emplace_back(std::move(task));
					}
				}

				if (!pending.empty()) {
					std::unique_lock<std::mutex> lock(mutex);
					while (!pending.empty()) {
						tasks.emplace_back(std::move(pending.front()));
						pending.pop_front();
					}
				}
			}

			m_tasks.store(nullptr, std::memory_order_release);
			m_mutex.store(nullptr, std::memory_order_release);
			m_condition.store(nullptr, std::memory_order_release);
		}

		CompletionService()
			: m_worker(&CompletionService::RunWorker, this) {
			m_tasks.wait(nullptr, std::memory_order::acquire);
		}

	public:
		~CompletionService() noexcept {
			m_worker.request_stop();
			if (auto condition = m_condition.load(std::memory_order::acquire)) {
				condition->notify_one();
			}
		}

		CompletionService(CompletionService const&) = delete;
		CompletionService& operator=(CompletionService const&) = delete;

		[[nodiscard]] static boost::intrusive_ptr<CompletionService> Instance() {
			static boost::intrusive_ptr<CompletionService> service(new CompletionService{});
			return service;
		}

		template <class Poll, class Complete>
		void Enqueue(Poll&& poll, Complete&& complete) {
			auto tasks = m_tasks.load(std::memory_order::acquire);
			auto mutex = m_mutex.load(std::memory_order::acquire);
			auto condition = m_condition.load(std::memory_order::acquire);
			{
				std::unique_lock<std::mutex> lock(*mutex);
				tasks->push_back({
					.Poll = std::forward<Poll>(poll),
					.Complete = std::forward<Complete>(complete)
				});
			}
			condition->notify_one();
		}
	};

}
