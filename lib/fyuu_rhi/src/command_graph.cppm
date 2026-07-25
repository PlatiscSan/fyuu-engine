module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#include <compare>
#include <exception>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
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
export import :command_graph_types;
export import :command_graph_builder;
import :resource;
import :view;
import :pipeline;

namespace fyuu_rhi::execution {

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
	};

	export template <class Backend> struct NativeCommandGraphBindings {
		std::vector<std::reference_wrapper<typename Backend::Resource const>> resources;
		std::vector<std::reference_wrapper<typename Backend::Pipeline const>> pipelines;
		std::vector<std::reference_wrapper<typename Backend::View const>> views;
		std::vector<std::reference_wrapper<typename Backend::PipelineResourceGroup const>> resource_groups;
		std::vector<typename Backend::PresentationTarget> presentation_targets;
	};

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

	export struct GraphCompletion {
		void* operation;
		void (*SetValue)(void*) noexcept;
		void (*SetError)(void*, std::exception_ptr const&) noexcept;
		void (*SetStopped)(void*) noexcept;
	};

	template <class Backend> class CommandGraphSender;

	export template <class Backend, class Receiver> class CommandGraphOperationState {
	private:
		Scheduler<Backend> m_scheduler;
		ExecutableGraph<Backend> m_graph;
		Receiver m_receiver;
		std::optional<typename Backend::GraphExecution> m_execution;
		bool m_started = false;

		static void CompleteValue(void* operation) noexcept {
			auto& self = *static_cast<CommandGraphOperationState*>(operation);
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_value(std::move(self.m_receiver));
#else
			std::move(self.m_receiver).set_value();
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

		static void CompleteError(void* operation, std::exception_ptr const& error) noexcept {
			auto& self = *static_cast<CommandGraphOperationState*>(operation);
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_error(std::move(self.m_receiver), error);
#else
			std::move(self.m_receiver).set_error(error);
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

		static void CompleteStopped(void* operation) noexcept {
			auto& self = *static_cast<CommandGraphOperationState*>(operation);
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_stopped(std::move(self.m_receiver));
#else
			std::move(self.m_receiver).set_stopped();
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using operation_state_concept = std::execution::operation_state_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

		CommandGraphOperationState(
			Scheduler<Backend> const& scheduler,
			ExecutableGraph<Backend> const& graph,
			Receiver const& receiver
		) : m_scheduler(scheduler),
			m_graph(graph),
			m_receiver(receiver) {

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

			GraphCompletion completion{
				.operation = this,
				.SetValue = CompleteValue,
				.SetError = CompleteError,
				.SetStopped = CompleteStopped
			};
			try {
				m_execution.emplace(CreateGraphExecution(
					m_scheduler.GetImplementation(),
					m_graph.GetImplementation()
				));
				StartGraphExecution(*m_execution, completion);
			}
			catch (...) {
				CompleteError(this, std::current_exception());
			}
		}
	};

	export template <class Backend> class CommandGraphSender {
	private:
		Scheduler<Backend> m_scheduler;
		ExecutableGraph<Backend> m_graph;

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using sender_concept = std::execution::sender_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

		CommandGraphSender(
			Scheduler<Backend> const& scheduler,
			ExecutableGraph<Backend> const& graph
		) noexcept : m_scheduler(scheduler),
			m_graph(graph) {

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
		auto connect(Receiver const& receiver) const {
			return CommandGraphOperationState<Backend, Receiver>(
				m_scheduler,
				m_graph,
				receiver
			);
		}
	};

	export template <class Backend>
	CommandGraphSender<Backend> Submit(
		Scheduler<Backend> const& scheduler,
		ExecutableGraph<Backend> const& graph
	) noexcept {
		return CommandGraphSender<Backend>(scheduler, graph);
	}

}
