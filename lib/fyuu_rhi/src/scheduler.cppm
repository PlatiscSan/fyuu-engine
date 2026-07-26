module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <atomic>
#include <concepts>
#include <exception>
#include <optional>
#include <stop_token>
#include <type_traits>
#include <utility>
#endif // !defined(__cpp_lib_modules)
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
#include <execution>
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

export module fyuu_rhi:scheduler;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :scheduler_types;
import :resource_submission;

namespace fyuu_rhi {
	template <class Backend> class Resource;
}

namespace fyuu_rhi::execution {

	template <class Backend> class Scheduler;
	template <class Backend, class Receiver> class CommandGraphOperationState;
	template <class Backend, class Receiver> class ResourceMapOperationState;
	template <class Backend, class Receiver> class ResourceUnmapOperationState;
	template <class Backend, class Receiver> class ResourceUploadOperationState;
	template <class Backend, class Receiver> class ResourceReadbackOperationState;
	template <class Backend> class ResourceRetirement;
	template <class Backend> class MappedResource;
	template <class Backend> class AbandonedResourceMapping;

	export template <class Backend> class ScheduleEnvironment {
	private:
		Scheduler<Backend> m_scheduler;

	public:
		explicit ScheduleEnvironment(Scheduler<Backend> const& scheduler) noexcept
			: m_scheduler(scheduler) {

		}

#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		[[nodiscard]]
		Scheduler<Backend> query(
			std::execution::get_completion_scheduler_t<std::execution::set_value_t>
		) const noexcept {
			return m_scheduler;
		}
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
	};

	template <class Backend, class Receiver> class ScheduleOperationState {
	private:
		Scheduler<Backend> m_scheduler;
		Receiver m_receiver;
		std::stop_token m_stop_token;
		std::atomic_bool m_stop_requested = false;
		std::atomic_bool m_completed = false;
		bool m_started = false;

		struct StopRequested {
			ScheduleOperationState* operation;

			void operator()() const noexcept {
				operation->m_stop_requested.store(true, std::memory_order::release);
			}
		};

		std::optional<std::stop_callback<StopRequested>> m_stop_callback;

		[[nodiscard]] static std::stop_token GetStopToken(Receiver const& receiver) noexcept {
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			if constexpr (requires {
				std::stop_token{
					std::execution::get_stop_token(std::execution::get_env(receiver))
				};
			}) {
				return std::stop_token{
					std::execution::get_stop_token(std::execution::get_env(receiver))
				};
			}
			else
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			if constexpr (requires { std::stop_token{ receiver.get_stop_token() }; }) {
				return std::stop_token{ receiver.get_stop_token() };
			}
			else if constexpr (requires {
				std::stop_token{ receiver.get_env().get_stop_token() };
			}) {
				return std::stop_token{ receiver.get_env().get_stop_token() };
			}
			else {
				return {};
			}
		}

		static void CompleteValue(void* operation) noexcept {
			auto& self = *static_cast<ScheduleOperationState*>(operation);
			if (self.m_stop_requested.load(std::memory_order::acquire)) {
				CompleteStopped(operation);
				return;
			}
			if (self.m_completed.exchange(true, std::memory_order::acq_rel)) {
				return;
			}
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_value(std::move(self.m_receiver));
#else
			std::move(self.m_receiver).set_value();
#endif
		}

		static void CompleteError(void* operation, std::exception_ptr const& error) noexcept {
			auto& self = *static_cast<ScheduleOperationState*>(operation);
			self.m_scheduler.m_failure_state->Fail(error);
			if (self.m_completed.exchange(true, std::memory_order::acq_rel)) {
				return;
			}
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_error(std::move(self.m_receiver), error);
#else
			std::move(self.m_receiver).set_error(error);
#endif
		}

		static void CompleteStopped(void* operation) noexcept {
			auto& self = *static_cast<ScheduleOperationState*>(operation);
			if (self.m_completed.exchange(true, std::memory_order::acq_rel)) {
				return;
			}
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_stopped(std::move(self.m_receiver));
#else
			std::move(self.m_receiver).set_stopped();
#endif
		}

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using operation_state_concept = std::execution::operation_state_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

		template <class R>
		ScheduleOperationState(Scheduler<Backend> const& scheduler, R&& receiver)
			: m_scheduler(scheduler),
			m_receiver(std::forward<R>(receiver)),
			m_stop_token(GetStopToken(m_receiver)) {

		}

		ScheduleOperationState(ScheduleOperationState const&) = delete;
		ScheduleOperationState(ScheduleOperationState&&) = delete;
		ScheduleOperationState& operator=(ScheduleOperationState const&) = delete;
		ScheduleOperationState& operator=(ScheduleOperationState&&) = delete;

		void start() & noexcept {
			if (m_started) {
				std::terminate();
			}
			m_started = true;
			m_stop_callback.emplace(m_stop_token, StopRequested{ this });
			if (m_stop_requested.load(std::memory_order::acquire)) {
				CompleteStopped(this);
				return;
			}
			SchedulerCompletion completion{
				.operation = this,
				.SetValue = CompleteValue,
				.SetError = CompleteError,
				.SetStopped = CompleteStopped
			};
			try {
				if (auto error = m_scheduler.m_failure_state->Error()) {
					CompleteError(this, error);
					return;
				}
				StartSchedulerExecution(m_scheduler.GetImplementation(), completion);
			}
			catch (...) {
				CompleteError(this, std::current_exception());
			}
		}
	};

	template <class Backend> class Sender {
	private:
		Scheduler<Backend> m_scheduler;

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using sender_concept = std::execution::sender_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

		explicit Sender(Scheduler<Backend> const& scheduler) noexcept
			: m_scheduler(scheduler) {

		}

		[[nodiscard]]
		ScheduleEnvironment<Backend> get_env() const noexcept {
			return ScheduleEnvironment<Backend>(m_scheduler);
		}

#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		template <class... Env>
		auto get_completion_signatures(Env&&...) const noexcept
			-> std::execution::completion_signatures<
				std::execution::set_value_t(),
				std::execution::set_error_t(std::exception_ptr),
				std::execution::set_stopped_t()
			> {
			return {};
		}
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

		template <class Receiver>
		auto connect(Receiver&& receiver) const {
			using ReceiverType = std::remove_cvref_t<Receiver>;
			return ScheduleOperationState<Backend, ReceiverType>(
				m_scheduler,
				std::forward<Receiver>(receiver)
			);
		}
	};

	export template <class Backend> class Scheduler {
	public:
		using Implementation = typename Backend::Scheduler;

#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using scheduler_concept = std::execution::scheduler_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
	private:
		template <class U, class Receiver> friend class ScheduleOperationState;
		template <class U, class Receiver> friend class CommandGraphOperationState;
		template <class U, class Receiver> friend class ResourceMapOperationState;
		template <class U, class Receiver> friend class ResourceUnmapOperationState;
		template <class U, class Receiver> friend class ResourceUploadOperationState;
		template <class U, class Receiver> friend class ResourceReadbackOperationState;
		template <class U> friend class ResourceRetirement;
		template <class U> friend class MappedResource;
		template <class U> friend class AbandonedResourceMapping;

		Implementation m_impl;
		std::shared_ptr<ResourceSubmissionCoordinator> m_submission_coordinator;
		std::shared_ptr<ExecutionFailureState> m_failure_state;

	public:
		explicit Scheduler(Implementation const& impl)
			: m_impl(impl),
			m_submission_coordinator(ResourceSubmissionCoordinator::Instance()),
			m_failure_state(std::make_shared<ExecutionFailureState>()) {
			static_assert(std::is_nothrow_copy_constructible_v<Implementation>);
		}

		Scheduler(Scheduler const&) noexcept = default;
		Scheduler(Scheduler&&) noexcept = default;
		Scheduler& operator=(Scheduler const&) noexcept = default;
		Scheduler& operator=(Scheduler&&) noexcept = default;
		~Scheduler() noexcept = default;

		[[nodiscard]]
		Sender<Backend> schedule() const noexcept {
			return Sender<Backend>(*this);
		}

		[[nodiscard]] Implementation const& GetImplementation() const noexcept {
			return m_impl;
		}

		bool operator==(Scheduler const& rhs) const noexcept {
			return m_impl == rhs.m_impl;
		}

		friend void swap(Scheduler& lhs, Scheduler& rhs) noexcept {
			using std::swap;
			swap(lhs.m_impl, rhs.m_impl);
			swap(lhs.m_submission_coordinator, rhs.m_submission_coordinator);
			swap(lhs.m_failure_state, rhs.m_failure_state);
		}
	};

}
