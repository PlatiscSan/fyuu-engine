#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>
#include "internal/resource_submission.hpp"

namespace {
	using fyuu_rhi::execution::ExecutionFailureState;
	using fyuu_rhi::execution::ResourceSubmissionAccess;
	using fyuu_rhi::execution::ResourceSubmissionCoordinator;

	struct Operation {
		std::vector<int>* starts;
		int id;
	};

	void StartOperation(
		void* operation,
		std::shared_ptr<ResourceSubmissionCoordinator::Ticket> const&
	) noexcept {
		auto* value = static_cast<Operation*>(operation);
		value->starts->push_back(value->id);
	}

	void Require(bool condition, std::string_view message) {
		if (!condition) {
			throw std::runtime_error(message.data());
		}
	}

	auto Enqueue(
		ResourceSubmissionCoordinator& coordinator,
		Operation& operation,
		std::initializer_list<ResourceSubmissionAccess> accesses
	) {
		return coordinator.Enqueue(
			std::span<ResourceSubmissionAccess const>(accesses.begin(), accesses.size()),
			&operation,
			StartOperation
		);
	}

	void ReadReadRunsConcurrently() {
		ResourceSubmissionCoordinator coordinator;
		std::uint64_t resource = 1u;
		std::vector<int> starts;
		Operation first{ &starts, 1 };
		Operation second{ &starts, 2 };
		auto first_ticket = Enqueue(coordinator, first, { { resource, false } });
		auto second_ticket = Enqueue(coordinator, second, { { resource, false } });
		coordinator.Activate(first_ticket);
		coordinator.Activate(second_ticket);
		Require(starts == std::vector{ 1, 2 }, "read/read submissions did not run concurrently");
		coordinator.Complete(first_ticket);
		coordinator.Complete(second_ticket);
	}

	void WriteReadWaits() {
		ResourceSubmissionCoordinator coordinator;
		std::uint64_t resource = 1u;
		std::vector<int> starts;
		Operation writer{ &starts, 1 };
		Operation reader{ &starts, 2 };
		auto writer_ticket = Enqueue(coordinator, writer, { { resource, true } });
		auto reader_ticket = Enqueue(coordinator, reader, { { resource, false } });
		coordinator.Activate(writer_ticket);
		coordinator.Activate(reader_ticket);
		Require(starts == std::vector{ 1 }, "reader started before writer completed");
		coordinator.Complete(writer_ticket);
		Require(starts == std::vector{ 1, 2 }, "reader did not start after writer completed");
		coordinator.Complete(reader_ticket);
	}

	void ReadersBlockWriter() {
		ResourceSubmissionCoordinator coordinator;
		std::uint64_t resource = 1u;
		std::vector<int> starts;
		Operation first{ &starts, 1 };
		Operation second{ &starts, 2 };
		Operation writer{ &starts, 3 };
		auto first_ticket = Enqueue(coordinator, first, { { resource, false } });
		auto second_ticket = Enqueue(coordinator, second, { { resource, false } });
		auto writer_ticket = Enqueue(coordinator, writer, { { resource, true } });
		coordinator.Activate(first_ticket);
		coordinator.Activate(second_ticket);
		coordinator.Activate(writer_ticket);
		Require(starts == std::vector{ 1, 2 }, "writer started while readers were active");
		coordinator.Complete(first_ticket);
		Require(starts == std::vector{ 1, 2 }, "writer ignored the second reader");
		coordinator.Complete(second_ticket);
		Require(starts == std::vector{ 1, 2, 3 }, "writer did not start after all readers completed");
		coordinator.Complete(writer_ticket);
	}

	void DuplicateDependenciesAreCollapsed() {
		ResourceSubmissionCoordinator coordinator;
		std::uint64_t first_resource = 1u;
		std::uint64_t second_resource = 2u;
		std::vector<int> starts;
		Operation writer{ &starts, 1 };
		Operation reader{ &starts, 2 };
		auto writer_ticket = Enqueue(
			coordinator,
			writer,
			{ { first_resource, true }, { second_resource, true } }
		);
		auto reader_ticket = Enqueue(
			coordinator,
			reader,
			{ { first_resource, false }, { second_resource, false } }
		);
		coordinator.Activate(writer_ticket);
		coordinator.Activate(reader_ticket);
		Require(reader_ticket->pending == 1u, "duplicate predecessor was counted more than once");
		coordinator.Complete(writer_ticket);
		Require(starts == std::vector{ 1, 2 }, "collapsed dependency did not release reader");
		coordinator.Complete(reader_ticket);
	}

	void WaitingSubmissionCanBeCancelled() {
		ResourceSubmissionCoordinator coordinator;
		std::uint64_t resource = 1u;
		std::vector<int> starts;
		Operation writer{ &starts, 1 };
		Operation reader{ &starts, 2 };
		auto writer_ticket = Enqueue(coordinator, writer, { { resource, true } });
		auto reader_ticket = Enqueue(coordinator, reader, { { resource, false } });
		coordinator.Activate(writer_ticket);
		coordinator.Activate(reader_ticket);
		Require(coordinator.Cancel(reader_ticket), "waiting reader could not be cancelled");
		coordinator.Complete(writer_ticket);
		Require(starts == std::vector{ 1 }, "cancelled reader was started");
		Require(reader_ticket->completed, "cancelled reader did not retire with its predecessor");
	}

	void CancelledWriterDoesNotBreakEarlierReaders() {
		ResourceSubmissionCoordinator coordinator;
		std::uint64_t resource = 1u;
		std::vector<int> starts;
		Operation reader{ &starts, 1 };
		Operation cancelled_writer{ &starts, 2 };
		Operation following_writer{ &starts, 3 };
		auto reader_ticket = Enqueue(coordinator, reader, { { resource, false } });
		auto cancelled_ticket = Enqueue(coordinator, cancelled_writer, { { resource, true } });
		auto following_ticket = Enqueue(coordinator, following_writer, { { resource, true } });
		coordinator.Activate(reader_ticket);
		coordinator.Activate(cancelled_ticket);
		coordinator.Activate(following_ticket);
		Require(coordinator.Cancel(cancelled_ticket), "waiting writer could not be cancelled");
		Require(starts == std::vector{ 1 }, "following writer bypassed the active reader");
		coordinator.Complete(reader_ticket);
		Require(starts == std::vector{ 1, 3 }, "following writer was not released after cancellation chain retired");
		coordinator.Complete(following_ticket);
	}

	void StartedSubmissionCannotBeCancelled() {
		ResourceSubmissionCoordinator coordinator;
		std::uint64_t resource = 1u;
		std::vector<int> starts;
		Operation writer{ &starts, 1 };
		auto ticket = Enqueue(coordinator, writer, { { resource, true } });
		coordinator.Activate(ticket);
		Require(!coordinator.Cancel(ticket), "started submission was cancelled prematurely");
		coordinator.Complete(ticket);
	}

	struct RetirementOperation {
		ResourceSubmissionCoordinator* coordinator;
		bool* destroyed;
	};

	void StartRetirement(
		void* operation,
		std::shared_ptr<ResourceSubmissionCoordinator::Ticket> const& ticket
	) noexcept {
		auto* retirement = static_cast<RetirementOperation*>(operation);
		*retirement->destroyed = true;
		retirement->coordinator->Complete(ticket);
	}

	void RetirementWaitsForResourceUsers() {
		ResourceSubmissionCoordinator coordinator;
		std::uint64_t resource = 1u;
		std::vector<int> starts;
		Operation writer{ &starts, 1 };
		bool destroyed = false;
		RetirementOperation retirement{ &coordinator, &destroyed };
		auto writer_ticket = Enqueue(coordinator, writer, { { resource, true } });
		ResourceSubmissionAccess access{ resource, true };
		auto retirement_ticket = coordinator.Enqueue(
			std::span<ResourceSubmissionAccess const>(&access, 1u),
			&retirement,
			StartRetirement
		);
		coordinator.Activate(writer_ticket);
		coordinator.Activate(retirement_ticket);
		Require(!destroyed, "resource was retired while a writer was active");
		coordinator.Complete(writer_ticket);
		Require(destroyed, "resource was not retired after its final user completed");
		Require(retirement_ticket->completed, "resource retirement ticket did not complete");
	}

	void ExecutionFailureKeepsFirstError() {
		ExecutionFailureState failure;
		std::exception_ptr first;
		std::exception_ptr second;
		try {
			throw std::runtime_error("first");
		}
		catch (...) {
			first = std::current_exception();
		}
		try {
			throw std::runtime_error("second");
		}
		catch (...) {
			second = std::current_exception();
		}
		failure.Fail(first);
		failure.Fail(second);
		Require(failure.Error() == first, "execution failure did not preserve the first error");
	}

	struct ConcurrentOperation {
		std::atomic_bool started = false;
		std::atomic_bool cancelled = false;
		std::atomic_int* active;
		std::atomic_int* maximum_active;

		ConcurrentOperation(
			std::atomic_int& active_,
			std::atomic_int& maximum_active_
		) noexcept : active(&active_),
			maximum_active(&maximum_active_) {

		}
	};

	void UpdateMaximum(std::atomic_int& maximum, int value) noexcept {
		auto current = maximum.load(std::memory_order::relaxed);
		while (current < value && !maximum.compare_exchange_weak(
			current,
			value,
			std::memory_order::relaxed
		)) {

		}
	}

	void StartConcurrentOperation(
		void* operation,
		std::shared_ptr<ResourceSubmissionCoordinator::Ticket> const&
	) noexcept {
		auto* value = static_cast<ConcurrentOperation*>(operation);
		auto active = value->active->fetch_add(1, std::memory_order::acq_rel) + 1;
		UpdateMaximum(*value->maximum_active, active);
		value->started.store(true, std::memory_order::release);
	}

	bool WaitUntilStarted(
		ConcurrentOperation const& operation,
		std::chrono::steady_clock::time_point deadline
	) noexcept {
		while (!operation.started.load(std::memory_order::acquire)) {
			if (std::chrono::steady_clock::now() >= deadline) {
				return false;
			}
			std::this_thread::yield();
		}
		return true;
	}

	void ConcurrentWritersRemainSerialized() {
		constexpr std::size_t count = 128u;
		ResourceSubmissionCoordinator coordinator;
		std::uint64_t resource = 1u;
		std::atomic_int active = 0;
		std::atomic_int maximum_active = 0;
		std::atomic_bool timed_out = false;
		std::barrier start(static_cast<std::ptrdiff_t>(count));
		std::vector<std::unique_ptr<ConcurrentOperation>> operations;
		std::vector<std::shared_ptr<ResourceSubmissionCoordinator::Ticket>> tickets;
		operations.reserve(count);
		tickets.reserve(count);
		for (std::size_t index = 0u; index < count; ++index) {
			operations.push_back(std::make_unique<ConcurrentOperation>(active, maximum_active));
			ResourceSubmissionAccess access{ resource, true };
			tickets.push_back(coordinator.Enqueue(
				std::span<ResourceSubmissionAccess const>(&access, 1u),
				operations.back().get(),
				StartConcurrentOperation
			));
		}
		{
			std::vector<std::jthread> workers;
			workers.reserve(count);
			for (std::size_t index = 0u; index < count; ++index) {
				auto RunWriter = [&, index]() noexcept {
					start.arrive_and_wait();
					coordinator.Activate(tickets[index]);
					auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
					if (!WaitUntilStarted(*operations[index], deadline)) {
						timed_out.store(true, std::memory_order::release);
						return;
					}
					std::this_thread::yield();
					active.fetch_sub(1, std::memory_order::acq_rel);
					coordinator.Complete(tickets[index]);
				};
				workers.emplace_back(RunWriter);
			}
		}
		Require(!timed_out.load(std::memory_order::acquire), "concurrent writer chain timed out");
		Require(maximum_active.load(std::memory_order::acquire) == 1, "concurrent writers overlapped");
		Require(active.load(std::memory_order::acquire) == 0, "concurrent writer remained active");
		for (auto const& ticket : tickets) {
			Require(ticket->completed, "concurrent writer did not retire");
		}
	}

	void ConcurrentSubmissionAndCancellationStress() {
		constexpr std::size_t count = 192u;
		ResourceSubmissionCoordinator coordinator;
		std::uint64_t resource = 1u;
		std::atomic_int active = 0;
		std::atomic_int maximum_active = 0;
		std::atomic_bool timed_out = false;
		std::barrier start(static_cast<std::ptrdiff_t>(count));
		std::vector<std::unique_ptr<ConcurrentOperation>> operations;
		operations.reserve(count);
		for (std::size_t index = 0u; index < count; ++index) {
			operations.push_back(std::make_unique<ConcurrentOperation>(active, maximum_active));
		}
		{
			std::vector<std::jthread> workers;
			workers.reserve(count);
			for (std::size_t index = 0u; index < count; ++index) {
				auto RunSubmission = [&, index]() noexcept {
					start.arrive_and_wait();
					ResourceSubmissionAccess access{ resource, true };
					auto ticket = coordinator.Enqueue(
						std::span<ResourceSubmissionAccess const>(&access, 1u),
						operations[index].get(),
						StartConcurrentOperation
					);
					coordinator.Activate(ticket);
					if (index % 3u != 0u && coordinator.Cancel(ticket)) {
						operations[index]->cancelled.store(true, std::memory_order::release);
						return;
					}
					auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
					if (!WaitUntilStarted(*operations[index], deadline)) {
						timed_out.store(true, std::memory_order::release);
						return;
					}
					std::this_thread::yield();
					active.fetch_sub(1, std::memory_order::acq_rel);
					coordinator.Complete(ticket);
				};
				workers.emplace_back(RunSubmission);
			}
		}
		Require(!timed_out.load(std::memory_order::acquire), "mixed submission stress test timed out");
		Require(maximum_active.load(std::memory_order::acquire) == 1, "mixed writer submissions overlapped");
		Require(active.load(std::memory_order::acquire) == 0, "mixed submission remained active");
		for (auto const& operation : operations) {
			if (operation->cancelled.load(std::memory_order::acquire)) {
				Require(
					!operation->started.load(std::memory_order::acquire),
					"successfully cancelled submission was started"
				);
			}
		}
	}

	struct TestCase {
		std::string_view name;
		void (*Run)();
	};
}

int main() {
	TestCase tests[] = {
		{ "read/read", ReadReadRunsConcurrently },
		{ "write/read", WriteReadWaits },
		{ "readers/write", ReadersBlockWriter },
		{ "dependency collapse", DuplicateDependenciesAreCollapsed },
		{ "waiting cancellation", WaitingSubmissionCanBeCancelled },
		{ "cancelled writer ordering", CancelledWriterDoesNotBreakEarlierReaders },
		{ "started cancellation", StartedSubmissionCannotBeCancelled },
		{ "resource retirement", RetirementWaitsForResourceUsers },
		{ "execution failure", ExecutionFailureKeepsFirstError },
		{ "concurrent writers", ConcurrentWritersRemainSerialized },
		{ "concurrent cancellation", ConcurrentSubmissionAndCancellationStress }
	};

	for (auto const& test : tests) {
		try {
			test.Run();
		}
		catch (std::exception const& error) {
			std::cerr << test.name << ": " << error.what() << '\n';
			return 1;
		}
	}
	return 0;
}
