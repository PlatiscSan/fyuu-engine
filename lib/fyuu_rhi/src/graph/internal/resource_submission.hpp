#pragma once
#if !defined(FYUU_RHI_RESOURCE_SUBMISSION_STD_INCLUDED)
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <ranges>
#include <span>
#include <unordered_map>
#include <vector>
#endif // !defined(FYUU_RHI_RESOURCE_SUBMISSION_STD_INCLUDED)

namespace fyuu_rhi::execution {
	class ExecutionFailureState {
	private:
		std::exception_ptr m_error;
		mutable std::mutex m_mutex;

	public:
		void Fail(std::exception_ptr const& error) noexcept {
			std::unique_lock<std::mutex> lock(m_mutex);
			if (!m_error) {
				m_error = error;
			}
		}

		[[nodiscard]] std::exception_ptr Error() const noexcept {
			std::unique_lock<std::mutex> lock(m_mutex);
			return m_error;
		}
	};

	struct ResourceSubmissionAccess {
		std::uint64_t resource = 0u;
		bool write = false;
	};

	class ResourceSubmissionCoordinator {
	public:
		struct Ticket {
			void* operation = nullptr;
			void (*Start)(void*, std::shared_ptr<Ticket> const&) noexcept = nullptr;
			std::size_t pending = 0u;
			bool active = false;
			bool started = false;
			bool cancelled = false;
			bool completed = false;
			std::vector<std::uint64_t> resources;
			std::vector<std::shared_ptr<Ticket>> dependents;
			std::shared_ptr<void> keep_alive;
		};

	private:
		struct ResourceState {
			std::shared_ptr<Ticket> writer;
			std::vector<std::shared_ptr<Ticket>> readers;
		};

		std::unordered_map<std::uint64_t, ResourceState> m_resources;
		std::mutex m_mutex;

		void FinishLocked(
			std::shared_ptr<Ticket> const& ticket,
			std::vector<std::shared_ptr<Ticket>>& ready
		) {
			ticket->completed = true;
			for (auto resource : ticket->resources) {
				auto iterator = m_resources.find(resource);
				if (iterator == m_resources.end()) {
					continue;
				}
				auto& state = iterator->second;
				if (state.writer == ticket) {
					state.writer.reset();
				}
				std::erase(state.readers, ticket);
				if (!state.writer && state.readers.empty()) {
					m_resources.erase(iterator);
				}
			}
			for (auto const& dependent : ticket->dependents) {
				if (dependent->completed) {
					continue;
				}
				--dependent->pending;
				if (dependent->pending != 0u) {
					continue;
				}
				if (dependent->cancelled) {
					FinishLocked(dependent, ready);
				}
				else if (dependent->active && !dependent->started) {
					dependent->started = true;
					ready.emplace_back(dependent);
				}
			}
			ticket->dependents.clear();
		}

		static void StartReady(
			std::vector<std::shared_ptr<Ticket>> const& ready
		) noexcept {
			for (auto const& ticket : ready) {
				ticket->Start(ticket->operation, ticket);
			}
		}

		static void AddDependency(
			std::shared_ptr<Ticket> const& predecessor,
			std::shared_ptr<Ticket> const& successor
		) {
			if (!predecessor || predecessor->completed ||
				std::ranges::contains(predecessor->dependents, successor)) {
				return;
			}
			predecessor->dependents.emplace_back(successor);
			++successor->pending;
		}

	public:
		[[nodiscard]] static std::shared_ptr<ResourceSubmissionCoordinator> Instance() {
			static auto coordinator = std::make_shared<ResourceSubmissionCoordinator>();
			return coordinator;
		}

		[[nodiscard]] std::shared_ptr<Ticket> Enqueue(
			std::span<ResourceSubmissionAccess const> accesses,
			void* operation,
			void (*Start)(void*, std::shared_ptr<Ticket> const&) noexcept
		) {
			auto ticket = std::make_shared<Ticket>(operation, Start);
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				for (auto const& access : accesses) {
					auto& state = m_resources[access.resource];
					ticket->resources.emplace_back(access.resource);
					AddDependency(state.writer, ticket);
					if (access.write) {
						for (auto const& reader : state.readers) {
							AddDependency(reader, ticket);
						}
						state.readers.clear();
						state.writer = ticket;
					}
					else {
						state.readers.emplace_back(ticket);
					}
				}
			}
			return ticket;
		}

		void Activate(std::shared_ptr<Ticket> const& ticket) noexcept {
			bool ready = false;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				if (ticket->completed) {
					return;
				}
				ticket->active = true;
				if (ticket->pending == 0u && !ticket->started) {
					ticket->started = true;
					ready = true;
				}
			}
			if (ready) {
				ticket->Start(ticket->operation, ticket);
			}
		}

		void Complete(std::shared_ptr<Ticket> const& ticket) noexcept {
			std::vector<std::shared_ptr<Ticket>> ready;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				if (!ticket || ticket->completed) {
					return;
				}
				FinishLocked(ticket, ready);
			}
			StartReady(ready);
		}

		[[nodiscard]] bool Cancel(std::shared_ptr<Ticket> const& ticket) noexcept {
			std::vector<std::shared_ptr<Ticket>> ready;
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				if (!ticket || ticket->completed || ticket->started) {
					return false;
				}
				ticket->cancelled = true;
				if (ticket->pending == 0u) {
					FinishLocked(ticket, ready);
				}
			}
			StartReady(ready);
			return true;
		}
	};
}
