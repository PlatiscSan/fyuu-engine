module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:resource_retirement;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource;
import :scheduler;
import :scheduler_types;
import :resource_submission;

namespace fyuu_rhi::execution {
	template <class Backend> class ResourceRetirement {
	private:
		using Ticket = ResourceSubmissionCoordinator::Ticket;

		Scheduler<Backend> m_scheduler;
		std::optional<Resource<Backend>> m_resource;
		std::weak_ptr<Ticket> m_ticket;
		DeferredDestroy m_deferred_destroy;

		static void Destroy(void* operation) noexcept {
			auto* self = static_cast<ResourceRetirement*>(operation);
			self->m_resource.reset();
		}

		static void Complete(void* operation) noexcept {
			auto* self = static_cast<ResourceRetirement*>(operation);
			auto ticket = self->m_ticket.lock();
			if (!ticket) {
				return;
			}
			auto keep_alive = ticket->keep_alive;
			self->m_scheduler.m_submission_coordinator->Complete(ticket);
			ticket->keep_alive.reset();
			(void)keep_alive;
		}

		static void CompleteError(void* operation, std::exception_ptr const&) noexcept {
			Destroy(operation);
			Complete(operation);
		}

		static void Start(
			void* operation,
			std::shared_ptr<Ticket> const& ticket
		) noexcept {
			auto* self = static_cast<ResourceRetirement*>(operation);
			self->m_ticket = ticket;
			try {
				StartDeferredDestroy(
					self->m_scheduler.GetImplementation(),
					self->m_deferred_destroy
				);
			}
			catch (...) {
				CompleteError(operation, std::current_exception());
			}
		}

	public:
		ResourceRetirement(
			Scheduler<Backend> const& scheduler,
			Resource<Backend>&& resource
		) : m_scheduler(scheduler),
			m_resource(std::move(resource)),
			m_deferred_destroy{
				.object = this,
				.Destroy = Destroy,
				.completion = {
					.operation = this,
					.SetValue = Complete,
					.SetError = CompleteError,
					.SetStopped = Complete
				}
			} {

		}

		void Submit(std::shared_ptr<ResourceRetirement> const& keep_alive, std::uint64_t identity) {
			ResourceSubmissionAccess access{ identity, true };
			auto ticket = m_scheduler.m_submission_coordinator->Enqueue(
				std::span<ResourceSubmissionAccess const>(&access, 1u),
				this,
				Start
			);
			ticket->keep_alive = keep_alive;
			m_scheduler.m_submission_coordinator->Activate(ticket);
		}
	};

	template <class Backend> void Retire(
		Scheduler<Backend> const& scheduler,
		Resource<Backend>&& resource
	) {
		if (resource.ID() == 0u) {
			return;
		}
		auto identity = resource.ID();
		auto retirement = std::make_shared<ResourceRetirement<Backend>>(
			scheduler,
			std::move(resource)
		);
		retirement->Submit(retirement, identity);
	}
}
