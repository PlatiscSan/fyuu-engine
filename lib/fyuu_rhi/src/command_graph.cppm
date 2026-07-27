module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <atomic>
#include <cstdint>
#include <compare>
#include <exception>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <type_traits>
#include <utility>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
#include <execution>
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

export module fyuu_rhi:command_graph;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :scheduler;
import :scheduler_types;
export import :command_graph_types;
export import :command_graph_builder;
import :resource;
import :view;
import :pipeline;
import :resource_submission;

namespace fyuu_rhi::execution {
	template <class Backend> struct NativeCommandGraphBindings;

	export template <class Backend> class CommandGraph {
	public:
		using Implementation = typename Backend::CommandGraph;

	private:
		Implementation m_impl;

	public:
		explicit CommandGraph(Implementation const& impl) noexcept
			: m_impl(impl) {

		}

		[[nodiscard]] Implementation const& GetImplementation() const noexcept {
			return m_impl;
		}
	};

	export template <class Backend> class CommandGraphBindings {
	private:
		template <class U, class Receiver> friend class CommandGraphOperationState;

		std::vector<Resource<Backend> const*> m_resources;
		std::vector<pipeline::Pipeline<Backend> const*> m_pipelines;
		std::vector<View<Backend> const*> m_views;
		std::vector<pipeline::PipelineResourceGroup<Backend> const*> m_resource_groups;
		std::vector<std::optional<typename Backend::PresentationTarget>> m_presentation_targets;

	public:
		explicit CommandGraphBindings(CommandGraphDescriptor const& descriptor)
			: m_resources(descriptor.resource_count, nullptr),
			m_pipelines(descriptor.pipeline_count, nullptr),
			m_views(descriptor.view_count, nullptr),
			m_resource_groups(descriptor.resource_group_count, nullptr),
			m_presentation_targets(descriptor.presentation_target_count) {

		}

		void Bind(GraphViewID id, View<Backend> const& view) {
			if (id.value >= m_views.size()) {
				throw std::out_of_range("CommandGraphBindings::Bind(): invalid view ID");
			}
			m_views[id.value] = &view;
		}

		void Bind(
			GraphPresentationID id,
			typename Backend::PresentationTarget const& presentation_target
		) {
			if (id.value >= m_presentation_targets.size()) {
				throw std::out_of_range("CommandGraphBindings::Bind(): invalid presentation target ID");
			}
			m_presentation_targets[id.value] = presentation_target;
		}

		void Bind(GraphResourceID id, Resource<Backend> const& resource) {
			if (id.value >= m_resources.size()) {
				throw std::out_of_range("CommandGraphBindings::Bind(): invalid resource ID");
			}
			m_resources[id.value] = &resource;
		}

		void Bind(GraphPipelineID id, pipeline::Pipeline<Backend> const& pipeline) {
			if (id.value >= m_pipelines.size()) {
				throw std::out_of_range("CommandGraphBindings::Bind(): invalid pipeline ID");
			}
			m_pipelines[id.value] = &pipeline;
		}

		void Bind(
			GraphResourceGroupID id,
			pipeline::PipelineResourceGroup<Backend> const& resource_group
		) {
			if (id.value >= m_resource_groups.size()) {
				throw std::out_of_range("CommandGraphBindings::Bind(): invalid resource group ID");
			}
			m_resource_groups[id.value] = &resource_group;
		}

		[[nodiscard]] std::span<Resource<Backend> const* const> Resources() const noexcept {
			return m_resources;
		}

		[[nodiscard]] std::span<pipeline::Pipeline<Backend> const* const> Pipelines() const noexcept {
			return m_pipelines;
		}

		[[nodiscard]] std::span<View<Backend> const* const> Views() const noexcept {
			return m_views;
		}

		[[nodiscard]] std::span<pipeline::PipelineResourceGroup<Backend> const* const>
		ResourceGroups() const noexcept {
			return m_resource_groups;
		}

		[[nodiscard]] std::span<std::optional<typename Backend::PresentationTarget> const>
		PresentationTargets() const noexcept {
			return m_presentation_targets;
		}

	private:
		[[nodiscard]] NativeCommandGraphBindings<Backend> Native() const;
	};

	template <class Backend> struct NativeCommandGraphBindings {
		std::vector<std::reference_wrapper<typename Backend::Resource const>> resources;
		std::vector<std::reference_wrapper<typename Backend::Pipeline const>> pipelines;
		std::vector<std::reference_wrapper<typename Backend::View const>> views;
		std::vector<std::reference_wrapper<typename Backend::PipelineResourceGroup const>> resource_groups;
		std::vector<typename Backend::PresentationTarget> presentation_targets;
	};

	template <class Backend>
	NativeCommandGraphBindings<Backend> CommandGraphBindings<Backend>::Native() const {
		NativeCommandGraphBindings<Backend> result;
		result.resources.reserve(m_resources.size());
		for (auto resource : m_resources) {
			if (!resource) {
				throw std::invalid_argument("CommandGraphBindings::Native(): graph resource is not bound");
			}
			result.resources.emplace_back(
				resource->GetLogicalDevicePassKey().GetImplementation()
			);
		}
		result.pipelines.reserve(m_pipelines.size());
		for (auto pipeline : m_pipelines) {
			if (!pipeline) {
				throw std::invalid_argument("CommandGraphBindings::Native(): graph pipeline is not bound");
			}
			result.pipelines.emplace_back(pipeline->GetPassKey().GetImplementation());
		}
		result.views.reserve(m_views.size());
		for (auto view : m_views) {
			if (!view) {
				throw std::invalid_argument("CommandGraphBindings::Native(): graph view is not bound");
			}
			result.views.emplace_back(view->GetPassKey().GetImplementation());
		}
		result.resource_groups.reserve(m_resource_groups.size());
		for (auto resource_group : m_resource_groups) {
			if (!resource_group) {
				throw std::invalid_argument("CommandGraphBindings::Native(): resource group is not bound");
			}
			result.resource_groups.emplace_back(
				resource_group->GetPassKey().GetImplementation()
			);
		}
		result.presentation_targets.reserve(m_presentation_targets.size());
		for (auto const& presentation_target : m_presentation_targets) {
			if (!presentation_target) {
				throw std::invalid_argument("CommandGraphBindings::Native(): presentation target is not bound");
			}
			result.presentation_targets.emplace_back(*presentation_target);
		}
		return result;
	}

	export template <class Backend> class ExecutableGraph {
	public:
		using Implementation = typename Backend::ExecutableGraph;

	private:
		Implementation m_impl;

	public:
		explicit ExecutableGraph(Implementation const& impl) noexcept
			: m_impl(impl) {

		}

		[[nodiscard]] Implementation const& GetImplementation() const noexcept {
			return m_impl;
		}
	};

	using GraphCompletion = SchedulerCompletion;

	template <class Backend> class CommandGraphSender;

	template <class Backend, class Receiver> class CommandGraphOperationState {
	private:
		Scheduler<Backend> m_scheduler;
		ExecutableGraph<Backend> m_graph;
		CommandGraphBindings<Backend> m_bindings;
		Receiver m_receiver;
		std::optional<typename Backend::ExecutableGraph> m_bound_graph;
		std::optional<typename Backend::GraphExecution> m_execution;
		std::shared_ptr<ResourceSubmissionCoordinator::Ticket> m_submission;
		std::stop_token m_stop_token;
		std::atomic_bool m_stop_requested = false;
		std::atomic_bool m_completed = false;
		std::atomic_bool m_backend_started = false;
		bool m_started = false;

		struct StopRequested {
			CommandGraphOperationState* operation;

			void operator()() const noexcept {
				operation->RequestStop();
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

		void DeliverStopped() noexcept {
			if (m_completed.exchange(true, std::memory_order::acq_rel)) {
				return;
			}
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_stopped(std::move(m_receiver));
#else
			std::move(m_receiver).set_stopped();
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

		void RequestStop() noexcept {
			m_stop_requested.store(true, std::memory_order::release);
			if (m_scheduler.m_submission_coordinator->Cancel(m_submission)) {
				DeliverStopped();
			}
		}

		static void StartSubmission(
			void* operation,
			std::shared_ptr<ResourceSubmissionCoordinator::Ticket> const&
		) noexcept {
			auto* self = static_cast<CommandGraphOperationState*>(operation);
			if (auto error = self->m_scheduler.m_failure_state->Error()) {
				CompleteError(operation, error);
				return;
			}
			GraphCompletion completion{
				.operation = operation,
				.SetValue = CompleteValue,
				.SetError = CompleteError,
				.SetStopped = CompleteStopped
			};
			try {
				self->m_execution.emplace(CreateGraphExecution(
					self->m_scheduler.GetImplementation(),
					*self->m_bound_graph
				));
				self->m_backend_started.store(true, std::memory_order::release);
				StartGraphExecution(*self->m_execution, completion);
			}
			catch (...) {
				CompleteError(operation, std::current_exception());
			}
		}

		void ReleaseSubmission() noexcept {
			m_scheduler.m_submission_coordinator->Complete(m_submission);
		}

		static void CompleteValue(void* operation) noexcept {
			auto* self = static_cast<CommandGraphOperationState*>(operation);
			self->ReleaseSubmission();
			if (self->m_stop_requested.load(std::memory_order::acquire)) {
				self->DeliverStopped();
				return;
			}
			if (self->m_completed.exchange(true, std::memory_order::acq_rel)) {
				return;
			}
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_value(std::move(self->m_receiver));
#else
			std::move(self->m_receiver).set_value();
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

		static void CompleteError(void* operation, std::exception_ptr const& error) noexcept {
			auto* self = static_cast<CommandGraphOperationState*>(operation);
			if (self->m_backend_started.load(std::memory_order::acquire)) {
				self->m_scheduler.m_failure_state->Fail(error);
			}
			self->ReleaseSubmission();
			if (self->m_completed.exchange(true, std::memory_order::acq_rel)) {
				return;
			}
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_error(std::move(self->m_receiver), error);
#else
			std::move(self->m_receiver).set_error(error);
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

		static void CompleteStopped(void* operation) noexcept {
			auto* self = static_cast<CommandGraphOperationState*>(operation);
			self->ReleaseSubmission();
			self->DeliverStopped();
		}

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using operation_state_concept = std::execution::operation_state_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

		template <class R>
		CommandGraphOperationState(
			Scheduler<Backend> const& scheduler,
			ExecutableGraph<Backend> const& graph,
			CommandGraphBindings<Backend> const& bindings,
			R&& receiver
		) : m_scheduler(scheduler),
			m_graph(graph),
			m_bindings(bindings),
			m_receiver(std::forward<R>(receiver)),
			m_stop_token(GetStopToken(m_receiver)) {

		}

		CommandGraphOperationState(CommandGraphOperationState const&) = delete;
		CommandGraphOperationState(CommandGraphOperationState&&) = delete;
		CommandGraphOperationState& operator=(CommandGraphOperationState const&) = delete;
		CommandGraphOperationState& operator=(CommandGraphOperationState&&) = delete;

		void start() & noexcept {
			if (m_started) {
				std::terminate();
			}
			m_started = true;

			try {
				auto bindings = m_bindings.Native();
				m_bound_graph.emplace(BindExecutableGraph(
					m_graph.GetImplementation(),
					bindings
				));
				std::vector<std::uint8_t> access_modes(bindings.resources.size(), 0u);
				for (auto const& node : (*m_bound_graph)->impl->descriptor.nodes) {
					for (auto const& access : node.accesses) {
						auto& mode = access_modes[access.resource.value];
						if ((access.flags & GraphAccessFlagBits::Write) != GraphAccessFlagBits::None) {
							mode = 2u;
						}
						else if (mode == 0u) {
							mode = 1u;
						}
					}
				}
				std::vector<ResourceSubmissionAccess> accesses;
				accesses.reserve(bindings.resources.size());
				for (std::size_t index = 0u; index < access_modes.size(); ++index) {
					if (access_modes[index] == 0u) {
						continue;
					}
					accesses.emplace_back(
						m_bindings.Resources()[index]->ID(),
						access_modes[index] == 2u
					);
				}
				m_submission = m_scheduler.m_submission_coordinator->Enqueue(
					accesses,
					this,
					StartSubmission
				);
				m_stop_callback.emplace(m_stop_token, StopRequested{ this });
				if (m_completed.load(std::memory_order::acquire)) {
					return;
				}
				m_scheduler.m_submission_coordinator->Activate(m_submission);
			}
			catch (...) {
				auto error = std::current_exception();
				if (m_completed.exchange(true, std::memory_order::acq_rel)) {
					return;
				}
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
				std::execution::set_error(std::move(m_receiver), error);
#else
				std::move(m_receiver).set_error(error);
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			}
		}
	};

	export template <class Backend> class CommandGraphSender {
	private:
		Scheduler<Backend> m_scheduler;
		ExecutableGraph<Backend> m_graph;
		CommandGraphBindings<Backend> m_bindings;

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using sender_concept = std::execution::sender_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

		CommandGraphSender(
			Scheduler<Backend> const& scheduler,
			ExecutableGraph<Backend> const& graph,
			CommandGraphBindings<Backend> const& bindings
		) : m_scheduler(scheduler),
			m_graph(graph),
			m_bindings(bindings) {

		}

		[[nodiscard]] ScheduleEnvironment<Backend> get_env() const noexcept {
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
			return CommandGraphOperationState<Backend, ReceiverType>(
				m_scheduler,
				m_graph,
				m_bindings,
				std::forward<Receiver>(receiver)
			);
		}
	};

	export template <class Backend>
	CommandGraphSender<Backend> Submit(
		Scheduler<Backend> const& scheduler,
		ExecutableGraph<Backend> const& graph,
		CommandGraphBindings<Backend> const& bindings
	) {
		return CommandGraphSender<Backend>(scheduler, graph, bindings);
	}

}
