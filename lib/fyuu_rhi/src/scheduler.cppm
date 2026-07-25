module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <concepts>
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

namespace fyuu_rhi::execution {

	template <class Backend> class Scheduler;

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

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using operation_state_concept = std::execution::operation_state_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

		ScheduleOperationState(Scheduler<Backend> const& scheduler, Receiver const& receiver)
			: m_scheduler(scheduler),
			m_receiver(receiver) {

		}

		ScheduleOperationState(ScheduleOperationState const&) = delete;
		ScheduleOperationState(ScheduleOperationState&&) = delete;
		ScheduleOperationState& operator=(ScheduleOperationState const&) = delete;
		ScheduleOperationState& operator=(ScheduleOperationState&&) = delete;

		void start() & noexcept {
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_value(std::move(m_receiver));
#else
			std::move(m_receiver).set_value();
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
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
			-> std::execution::completion_signatures<std::execution::set_value_t()> {
			return {};
		}
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

		template <class Receiver>
		auto connect(Receiver const& receiver) const {
			return ScheduleOperationState<Backend, Receiver>(
				m_scheduler,
				receiver
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
		Implementation m_impl;

	public:
		explicit Scheduler(Implementation const& impl) noexcept
			: m_impl(impl) {
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

		friend bool operator==(Scheduler const&, Scheduler const&) noexcept = default;

		friend void swap(Scheduler& lhs, Scheduler& rhs) noexcept {
			using std::swap;
			swap(lhs.m_impl, rhs.m_impl);
		}
	};

}
