module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <atomic>
#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <type_traits>
#include <utility>
#endif // !defined(__cpp_lib_modules)
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
#include <execution>
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

export module fyuu_rhi:resource_mapping;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource;
import :resource_types;
import :scheduler;
import :scheduler_types;
import :resource_submission;
import :resource_retirement;

namespace fyuu_rhi::execution {
	template <class Backend, class Receiver> class ResourceMapOperationState;
	template <class Backend, class Receiver> class ResourceUnmapOperationState;

	template <class Backend> class AbandonedResourceMapping {
	private:
		Scheduler<Backend> m_scheduler;
		std::optional<Resource<Backend>> m_resource;
		std::shared_ptr<ResourceSubmissionCoordinator::Ticket> m_submission;
		ResourceUnmapRequest m_request;
		std::shared_ptr<AbandonedResourceMapping> m_keep_alive;

		static void Complete(void* operation) noexcept {
			auto& self = *static_cast<AbandonedResourceMapping*>(operation);
			auto keep_alive = self.m_keep_alive;
			auto resource = std::move(*self.m_resource);
			self.m_resource.reset();
			self.m_scheduler.m_submission_coordinator->Complete(self.m_submission);
			self.m_keep_alive.reset();
			Retire(self.m_scheduler, std::move(resource));
			(void)keep_alive;
		}

		static void CompleteError(void* operation, std::exception_ptr const&) noexcept {
			Complete(operation);
		}

	public:
		AbandonedResourceMapping(
			Scheduler<Backend> const& scheduler,
			Resource<Backend>&& resource,
			std::shared_ptr<ResourceSubmissionCoordinator::Ticket> const& submission,
			ResourceDataRange const& range,
			bool write
		) : m_scheduler(scheduler),
			m_resource(std::move(resource)),
			m_submission(submission),
			m_request{
				.offset = range.offset,
				.size = range.size,
				.write = write,
				.completion = {
					.operation = this,
					.SetValue = Complete,
					.SetError = CompleteError,
					.SetStopped = Complete
				}
			} {

		}

		void Start(std::shared_ptr<AbandonedResourceMapping> const& keep_alive) noexcept {
			m_keep_alive = keep_alive;
			try {
				StartUnmapResource(
					m_scheduler.GetImplementation(),
					m_resource->GetLogicalDevicePassKey().GetImplementation(),
					m_request
				);
			}
			catch (...) {
				CompleteError(this, std::current_exception());
			}
		}
	};

	export template <class Backend> class MappedResource {
	private:
		template <class U, class Receiver> friend class ResourceMapOperationState;
		template <class U, class Receiver> friend class ResourceUnmapOperationState;
		template <class U> friend class ResourceUnmapSender;

		Scheduler<Backend> m_scheduler;
		std::optional<Resource<Backend>> m_resource;
		std::shared_ptr<ResourceSubmissionCoordinator::Ticket> m_submission;
		std::byte* m_data = nullptr;
		ResourceDataRange m_range;
		ResourceMapFlags m_flags;

		MappedResource(
			Scheduler<Backend> const& scheduler,
			Resource<Backend>&& resource,
			std::shared_ptr<ResourceSubmissionCoordinator::Ticket> const& submission,
			std::byte* data,
			ResourceDataRange const& range,
			ResourceMapFlags const& flags
		) : m_scheduler(scheduler),
			m_resource(std::move(resource)),
			m_submission(submission),
			m_data(data),
			m_range(range),
			m_flags(flags) {

		}

	public:
		MappedResource(MappedResource const&) = delete;
		MappedResource& operator=(MappedResource const&) = delete;
		MappedResource(MappedResource&& other) noexcept
			: m_scheduler(other.m_scheduler),
			m_resource(std::move(other.m_resource)),
			m_submission(other.m_submission),
			m_data(other.m_data),
			m_range(other.m_range),
			m_flags(std::move(other.m_flags)) {
			other.m_resource.reset();
			other.m_submission.reset();
			other.m_data = nullptr;
			other.m_range = {};
		}
		MappedResource& operator=(MappedResource&&) = delete;
		~MappedResource() noexcept {
			if (m_resource) {
				auto abandoned = std::make_shared<AbandonedResourceMapping<Backend>>(
					m_scheduler,
					std::move(*m_resource),
					m_submission,
					m_range,
					m_flags.Test(ResourceMapFlagBits::Write)
				);
				m_resource.reset();
				abandoned->Start(abandoned);
			}
		}

		[[nodiscard]] std::span<std::byte const> Bytes() const noexcept {
			return { m_data, m_range.size };
		}

		[[nodiscard]] std::span<std::byte> WritableBytes() {
			if (!m_flags.Test(ResourceMapFlagBits::Write)) {
				throw std::logic_error("MappedResource is not writable");
			}
			return { m_data, m_range.size };
		}

		[[nodiscard]] ResourceDataRange Range() const noexcept {
			return m_range;
		}

		[[nodiscard]] std::uint64_t ID() const noexcept {
			return m_resource ? m_resource->ID() : 0u;
		}
	};

	template <class Receiver>
	[[nodiscard]] static std::stop_token ResourceDataStopToken(Receiver const& receiver) noexcept {
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

	template <class Backend, class Receiver> class ResourceMapOperationState {
	private:
		Scheduler<Backend> m_scheduler;
		std::optional<Resource<Backend>> m_resource;
		ResourceDataRange m_range;
		ResourceMapFlags m_flags;
		Receiver m_receiver;
		std::shared_ptr<ResourceSubmissionCoordinator::Ticket> m_submission;
		ResourceMapRequest m_request;
		std::stop_token m_stop_token;
		std::atomic_bool m_completed = false;
		bool m_started = false;

		struct StopRequested {
			ResourceMapOperationState* operation;

			void operator()() const noexcept {
				operation->RequestStop();
			}
		};

		std::optional<std::stop_callback<StopRequested>> m_stop_callback;

		void ReleaseSubmission() noexcept {
			m_scheduler.m_submission_coordinator->Complete(m_submission);
		}

		void DeliverStopped() noexcept {
			if (m_completed.exchange(true, std::memory_order::acq_rel)) {
				return;
			}
			if (m_resource) {
				auto resource = std::move(*m_resource);
				m_resource.reset();
				Retire(m_scheduler, std::move(resource));
			}
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_stopped(std::move(m_receiver));
#else
			std::move(m_receiver).set_stopped();
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

		void RequestStop() noexcept {
			if (m_scheduler.m_submission_coordinator->Cancel(m_submission)) {
				DeliverStopped();
			}
		}

		static void StartSubmission(
			void* operation,
			std::shared_ptr<ResourceSubmissionCoordinator::Ticket> const&
		) noexcept {
			auto& self = *static_cast<ResourceMapOperationState*>(operation);
			if (auto error = self.m_scheduler.m_failure_state->Error()) {
				CompleteError(operation, error);
				return;
			}
			try {
				StartMapResource(
					self.m_scheduler.GetImplementation(),
					self.m_resource->GetLogicalDevicePassKey().GetImplementation(),
					self.m_request
				);
			}
			catch (...) {
				CompleteError(operation, std::current_exception());
			}
		}

		static void CompleteValue(void* operation, std::byte* data) noexcept {
			auto& self = *static_cast<ResourceMapOperationState*>(operation);
			if (self.m_completed.exchange(true, std::memory_order::acq_rel)) {
				return;
			}
			MappedResource<Backend> mapped(
				self.m_scheduler,
				std::move(*self.m_resource),
				self.m_submission,
				data,
				self.m_range,
				self.m_flags
			);
			self.m_resource.reset();
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_value(std::move(self.m_receiver), std::move(mapped));
#else
			std::move(self.m_receiver).set_value(std::move(mapped));
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

		static void CompleteError(void* operation, std::exception_ptr const& error) noexcept {
			auto& self = *static_cast<ResourceMapOperationState*>(operation);
			if (self.m_completed.exchange(true, std::memory_order::acq_rel)) {
				return;
			}
			auto resource = std::move(*self.m_resource);
			self.m_resource.reset();
			self.ReleaseSubmission();
			Retire(self.m_scheduler, std::move(resource));
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_error(std::move(self.m_receiver), error);
#else
			std::move(self.m_receiver).set_error(error);
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using operation_state_concept = std::execution::operation_state_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

		template <class R>
		ResourceMapOperationState(
			Scheduler<Backend> const& scheduler,
			Resource<Backend>&& resource,
			ResourceDataRange const& range,
			ResourceMapFlags const& flags,
			R&& receiver
		) : m_scheduler(scheduler),
			m_resource(std::move(resource)),
			m_range(range),
			m_flags(flags),
			m_receiver(std::forward<R>(receiver)),
			m_request{
				.offset = range.offset,
				.size = range.size,
				.read = flags.Test(ResourceMapFlagBits::Read),
				.write = flags.Test(ResourceMapFlagBits::Write),
				.completion = {
					.operation = this,
					.SetValue = CompleteValue,
					.SetError = CompleteError
				}
			},
			m_stop_token(ResourceDataStopToken(m_receiver)) {

		}

		ResourceMapOperationState(ResourceMapOperationState const&) = delete;
		ResourceMapOperationState(ResourceMapOperationState&&) = delete;
		ResourceMapOperationState& operator=(ResourceMapOperationState const&) = delete;
		ResourceMapOperationState& operator=(ResourceMapOperationState&&) = delete;
		~ResourceMapOperationState() noexcept {
			if (m_resource) {
				auto resource = std::move(*m_resource);
				m_resource.reset();
				Retire(m_scheduler, std::move(resource));
			}
		}

		void start() & noexcept {
			if (m_started) {
				std::terminate();
			}
			m_started = true;
			try {
				if (!m_resource || m_resource->ID() == 0u) {
					throw std::invalid_argument("Map(): resource is empty");
				}
				if (m_range.size == 0u || m_range.offset > m_resource->Size() ||
					m_range.size > m_resource->Size() - m_range.offset) {
					throw std::out_of_range("Map(): resource data range is invalid");
				}
				bool read = m_flags.Test(ResourceMapFlagBits::Read);
				bool write = m_flags.Test(ResourceMapFlagBits::Write);
				if (read == write) {
					throw std::invalid_argument("Map(): select exactly one of Read or Write");
				}
				if (read && !m_resource->Flags().Test(ResourceFlagBits::DeviceReadback)) {
					throw std::invalid_argument("Map(): Read requires DeviceReadback");
				}
				if (write && !m_resource->Flags().Test(ResourceFlagBits::HostVisible)) {
					throw std::invalid_argument("Map(): Write requires HostVisible");
				}
				ResourceSubmissionAccess access{ m_resource->ID(), true };
				m_submission = m_scheduler.m_submission_coordinator->Enqueue(
					std::span<ResourceSubmissionAccess const>(&access, 1u),
					this,
					StartSubmission
				);
				m_stop_callback.emplace(m_stop_token, StopRequested{ this });
				if (m_stop_token.stop_requested()) {
					RequestStop();
					return;
				}
				m_scheduler.m_submission_coordinator->Activate(m_submission);
			}
			catch (...) {
				auto error = std::current_exception();
				if (m_submission) {
					ReleaseSubmission();
				}
				if (m_resource) {
					auto resource = std::move(*m_resource);
					m_resource.reset();
					Retire(m_scheduler, std::move(resource));
				}
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

	export template <class Backend> class ResourceMapSender {
	private:
		Scheduler<Backend> m_scheduler;
		Resource<Backend> m_resource;
		ResourceDataRange m_range;
		ResourceMapFlags m_flags;

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using sender_concept = std::execution::sender_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

		ResourceMapSender(
			Scheduler<Backend> const& scheduler,
			Resource<Backend>&& resource,
			ResourceDataRange const& range,
			ResourceMapFlags const& flags
		) : m_scheduler(scheduler),
			m_resource(std::move(resource)),
			m_range(range),
			m_flags(flags) {

		}

		ResourceMapSender(ResourceMapSender const&) = delete;
		ResourceMapSender& operator=(ResourceMapSender const&) = delete;
		ResourceMapSender(ResourceMapSender&&) noexcept = default;
		ResourceMapSender& operator=(ResourceMapSender&&) = delete;
		~ResourceMapSender() noexcept {
			if (m_resource.ID() != 0u) {
				Retire(m_scheduler, std::move(m_resource));
			}
		}

		[[nodiscard]] ScheduleEnvironment<Backend> get_env() const noexcept {
			return ScheduleEnvironment<Backend>(m_scheduler);
		}

#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		template <class... Env>
		auto get_completion_signatures(Env&&...) const noexcept
			-> std::execution::completion_signatures<
				std::execution::set_value_t(MappedResource<Backend>),
				std::execution::set_error_t(std::exception_ptr),
				std::execution::set_stopped_t()
			> {
			return {};
		}
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

		template <class Receiver>
		auto connect(Receiver&& receiver) && {
			using ReceiverType = std::remove_cvref_t<Receiver>;
			return ResourceMapOperationState<Backend, ReceiverType>(
				m_scheduler,
				std::move(m_resource),
				m_range,
				m_flags,
				std::forward<Receiver>(receiver)
			);
		}
	};

	template <class Backend, class Receiver> class ResourceUnmapOperationState {
	private:
		MappedResource<Backend> m_mapped;
		Receiver m_receiver;
		std::stop_token m_stop_token;
		ResourceUnmapRequest m_request;
		std::atomic_bool m_completed = false;
		bool m_started = false;

		static void CompleteValue(void* operation) noexcept {
			auto& self = *static_cast<ResourceUnmapOperationState*>(operation);
			if (self.m_stop_token.stop_requested()) {
				CompleteStopped(operation);
				return;
			}
			if (self.m_completed.exchange(true, std::memory_order::acq_rel)) {
				return;
			}
			self.m_mapped.m_scheduler.m_submission_coordinator->Complete(
				self.m_mapped.m_submission
			);
			auto resource = std::move(*self.m_mapped.m_resource);
			self.m_mapped.m_resource.reset();
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_value(std::move(self.m_receiver), std::move(resource));
#else
			std::move(self.m_receiver).set_value(std::move(resource));
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

		static void CompleteStopped(void* operation) noexcept {
			auto& self = *static_cast<ResourceUnmapOperationState*>(operation);
			if (self.m_completed.exchange(true, std::memory_order::acq_rel)) {
				return;
			}
			self.m_mapped.m_scheduler.m_submission_coordinator->Complete(
				self.m_mapped.m_submission
			);
			auto resource = std::move(*self.m_mapped.m_resource);
			self.m_mapped.m_resource.reset();
			Retire(self.m_mapped.m_scheduler, std::move(resource));
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_stopped(std::move(self.m_receiver));
#else
			std::move(self.m_receiver).set_stopped();
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

		static void CompleteError(void* operation, std::exception_ptr const& error) noexcept {
			auto& self = *static_cast<ResourceUnmapOperationState*>(operation);
			if (self.m_completed.exchange(true, std::memory_order::acq_rel)) {
				return;
			}
			auto resource = std::move(*self.m_mapped.m_resource);
			self.m_mapped.m_resource.reset();
			self.m_mapped.m_scheduler.m_submission_coordinator->Complete(
				self.m_mapped.m_submission
			);
			Retire(self.m_mapped.m_scheduler, std::move(resource));
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
			std::execution::set_error(std::move(self.m_receiver), error);
#else
			std::move(self.m_receiver).set_error(error);
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		}

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using operation_state_concept = std::execution::operation_state_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

		template <class R>
		ResourceUnmapOperationState(MappedResource<Backend>&& mapped, R&& receiver)
			: m_mapped(std::move(mapped)),
			m_receiver(std::forward<R>(receiver)),
			m_stop_token(ResourceDataStopToken(m_receiver)),
			m_request{
				.offset = m_mapped.m_range.offset,
				.size = m_mapped.m_range.size,
				.write = m_mapped.m_flags.Test(ResourceMapFlagBits::Write),
				.completion = {
					.operation = this,
					.SetValue = CompleteValue,
					.SetError = CompleteError,
					.SetStopped = CompleteStopped
				}
			} {

		}

		ResourceUnmapOperationState(ResourceUnmapOperationState const&) = delete;
		ResourceUnmapOperationState(ResourceUnmapOperationState&&) = delete;
		ResourceUnmapOperationState& operator=(ResourceUnmapOperationState const&) = delete;
		ResourceUnmapOperationState& operator=(ResourceUnmapOperationState&&) = delete;

		void start() & noexcept {
			if (m_started) {
				std::terminate();
			}
			m_started = true;
			try {
				StartUnmapResource(
					m_mapped.m_scheduler.GetImplementation(),
					m_mapped.m_resource->GetLogicalDevicePassKey().GetImplementation(),
					m_request
				);
			}
			catch (...) {
				CompleteError(this, std::current_exception());
			}
		}
	};

	export template <class Backend> class ResourceUnmapSender {
	private:
		MappedResource<Backend> m_mapped;

	public:
#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		using sender_concept = std::execution::sender_t;
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

		explicit ResourceUnmapSender(MappedResource<Backend>&& mapped)
			: m_mapped(std::move(mapped)) {

		}

		ResourceUnmapSender(ResourceUnmapSender const&) = delete;
		ResourceUnmapSender& operator=(ResourceUnmapSender const&) = delete;
		ResourceUnmapSender(ResourceUnmapSender&&) noexcept = default;
		ResourceUnmapSender& operator=(ResourceUnmapSender&&) = delete;

		[[nodiscard]] ScheduleEnvironment<Backend> get_env() const noexcept {
			return ScheduleEnvironment<Backend>(m_mapped.m_scheduler);
		}

#if defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L
		template <class... Env>
		auto get_completion_signatures(Env&&...) const noexcept
			-> std::execution::completion_signatures<
				std::execution::set_value_t(Resource<Backend>),
				std::execution::set_error_t(std::exception_ptr),
				std::execution::set_stopped_t()
			> {
			return {};
		}
#endif // defined(__cpp_lib_senders) && __cpp_lib_senders >= 202406L

		template <class Receiver>
		auto connect(Receiver&& receiver) && {
			using ReceiverType = std::remove_cvref_t<Receiver>;
			return ResourceUnmapOperationState<Backend, ReceiverType>(
				std::move(m_mapped),
				std::forward<Receiver>(receiver)
			);
		}
	};

	export template <class Backend> ResourceMapSender<Backend> Map(
		Scheduler<Backend> const& scheduler,
		Resource<Backend>&& resource,
		ResourceDataRange const& range,
		ResourceMapFlags const& flags
	) {
		return ResourceMapSender<Backend>(scheduler, std::move(resource), range, flags);
	}

	export template <class Backend> ResourceUnmapSender<Backend> Unmap(
		MappedResource<Backend>&& mapped
	) {
		return ResourceUnmapSender<Backend>(std::move(mapped));
	}
}
