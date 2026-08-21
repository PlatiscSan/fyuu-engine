module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <stdexcept>
#include <utility>
#include <deque>
#include <vector>
#include <algorithm>
#include <functional>
#include <cmath>
#include <limits>

#include <cstdint>
#include <tuple>
#include <mutex>

#include <optional>

#include <compare>
#include <ranges>
#include <span>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)
#define FYUU_RHI_USE_VULKAN_HEADER
#include <vulkan/vulkan_shared.hpp>
#endif // !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)

module fyuu_rhi:vulkan_queue_allocator;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
#if !defined(FYUU_RHI_USE_VULKAN_HEADER)
import vulkan;
#endif // !defined(FYUU_RHI_USE_VULKAN_HEADER)

namespace fyuu_rhi::vulkan {

	enum class CommandQueueType : std::uint8_t {
		None = 0u,
		Graphics = 1u << 0u,
		Compute = 1u << 1u,
		Copy = 1u << 2u,
	};

	constexpr CommandQueueType operator|(CommandQueueType lhs, CommandQueueType rhs) noexcept {
		return static_cast<CommandQueueType>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
	}

	constexpr CommandQueueType operator&(CommandQueueType lhs, CommandQueueType rhs) noexcept {
		return static_cast<CommandQueueType>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
	}

	constexpr CommandQueueType& operator|=(CommandQueueType& lhs, CommandQueueType rhs) noexcept {
		return lhs = lhs | rhs;
	}

	constexpr CommandQueueType& operator&=(CommandQueueType& lhs, CommandQueueType rhs) noexcept {
		return lhs = lhs & rhs;
	}

	/// Describes every queue created from one physical-device queue family.
	struct CommandQueueInfo {
		CommandQueueType capabilities;
		std::uint32_t family;
		std::uint32_t count;
	};

	/// Identifies one immutable queue slot created with the logical device.
	struct QueueIdentifier {
		std::uint32_t family;
		std::uint32_t index;

		std::strong_ordering operator<=>(QueueIdentifier const&) const noexcept = default;
	};

	/// Supplies one VkDeviceQueueCreateInfo without exposing allocator state.
	/// priorities remains valid while the QueueAllocator state remains alive.
	struct QueueCreatePlan {
		std::uint32_t family;
		std::span<float const> priorities;
	};

	/// Contains graph analysis produced by Scheduler. QueueAllocator consumes the
	/// complete request array and alone decides the physical queue assignment.
	struct QueueRequest {
		CommandQueueType required_capabilities;
		/// Normalized urgency in [0, 1].
		float priority;
		/// Relative work estimate comparable with other batches in this graph.
		std::uint64_t estimated_work;
		/// Soft affinity used to avoid unnecessary family ownership transfers.
		std::optional<std::uint32_t> preferred_family;
		/// Hard family restriction; empty means every compatible family is legal.
		std::span<std::uint32_t const> allowed_families;
		/// Predecessor indices in this same request array.
		std::span<std::size_t const> dependencies;
	};

}

namespace std {
	/// Packs the family/index pair for unordered containers.
	template <>
	struct hash<fyuu_rhi::vulkan::QueueIdentifier> {
		std::size_t operator()(fyuu_rhi::vulkan::QueueIdentifier const& identifier) const noexcept {
			return(static_cast<std::size_t>(identifier.family) << 32u) | static_cast<std::size_t>(identifier.index);
		}
	};
}

namespace {

	using namespace fyuu_rhi::vulkan;

	struct QueueFamilyState {
		CommandQueueInfo info;
		std::vector<float> priorities;
	};

	struct QueueState {
		QueueIdentifier identifier;
		CommandQueueType capabilities;
		float priority;
		/// Work assigned during phase 1 but not submitted successfully.
		std::uint64_t reserved_work = 0u;
		/// Submitted work whose GPU completion has not yet been retired.
		std::uint64_t submitted_work = 0u;
		/// Serializes externally synchronized host operations on this physical
		/// queue across every scheduler sharing the logical device.
		std::mutex submission_mutex;
	};

	struct QueueAllocatorState {
		std::vector<QueueFamilyState> families;
		std::vector<QueueCreatePlan> create_plans;
		/// QueueState contains a mutex and therefore has a stable, non-moving
		/// address in deque storage after logical-device creation.
		std::deque<QueueState> queues;
		/// Serializes whole-graph selection, reservation and load accounting.
		std::mutex mutex;
	};

	std::uint64_t NormalizedWork(std::uint64_t work) noexcept {
		return std::max(work, std::uint64_t{ 1u });
	}

	std::uint8_t CapabilityCount(CommandQueueType capabilities) noexcept {
		auto value = static_cast<std::uint8_t>(capabilities);
		std::uint8_t result = 0u;
		while (value != 0u) {
			result += value & 1u;
			value >>= 1u;
		}
		return result;
	}

	float NativePriority(std::uint32_t index, std::uint32_t count) noexcept {
		if (count == 1u) {
			return 1.0f;
		}
		return 1.0f - static_cast<float>(index) / static_cast<float>(count - 1u);
	}

	bool ContainsFamily(std::span<std::uint32_t const> families, std::uint32_t family) noexcept {
		return families.empty() || std::ranges::find(families, family) != families.end();
	}

	bool SupportsCapabilities(QueueState const& queue, CommandQueueType required) noexcept {
		return (queue.capabilities & required) == required;
	}

	void ValidateRequest(QueueRequest const& request, std::size_t index, std::size_t request_count) {
		if (request.required_capabilities == CommandQueueType::None &&
			request.allowed_families.empty()) {
			throw std::invalid_argument(
				"A capability-free Vulkan queue request requires allowed families"
			);
		}
		if (!std::isfinite(request.priority) || request.priority < 0.0f || request.priority > 1.0f) {
			throw std::invalid_argument("Vulkan queue request priority is outside [0, 1]");
		}
		for (std::size_t dependency : request.dependencies) {
			if (dependency >= request_count || dependency == index) {
				throw std::invalid_argument("Vulkan queue request contains an invalid dependency");
			}
		}
	}

}

namespace fyuu_rhi::vulkan {

	class QueueAllocator;
	class QueueReservationSession;

	/// Retains submitted load until CompletionToken releases it after GPU
	/// completion. The token performs no native wait; the owner must keep it
	/// alive at least as long as the corresponding completion point.
	class QueueWorkToken final {
	private:
		friend class QueueReservationSession;

		std::shared_ptr<QueueAllocatorState> state;
		std::size_t slot;
		std::uint64_t work;

		QueueWorkToken(
			std::shared_ptr<QueueAllocatorState> const& state_,
			std::size_t slot_,
			std::uint64_t work_
		) noexcept
			: state(state_),
			slot(slot_),
			work(work_) {
		}

		void Retire() noexcept {
			if (!state) {
				return;
			}
			std::unique_lock<std::mutex> lock(state->mutex);
			state->queues[slot].submitted_work -= work;
			state.reset();
		}

	public:
		QueueWorkToken(QueueWorkToken const&) = delete;
		QueueWorkToken& operator=(QueueWorkToken const&) = delete;

		QueueWorkToken(QueueWorkToken&& other) noexcept
			: state(std::move(other.state)),
			slot(std::exchange(other.slot, 0u)),
			work(std::exchange(other.work, 0u)) {
		}

		QueueWorkToken& operator=(QueueWorkToken&& other) noexcept {
			if (this != &other) {
				Retire();
				state = std::move(other.state);
				slot = std::exchange(other.slot, 0u);
				work = std::exchange(other.work, 0u);
			}
			return *this;
		}

		~QueueWorkToken() noexcept {
			Retire();
		}

		QueueIdentifier GetQueue() const noexcept {
			return state->queues[slot].identifier;
		}
	};

	/// Move-only stack object granting access to one physical queue. Different
	/// graphs may select the same queue; native queue calls are serialized by the
	/// QueueState submission mutex while recording remains fully concurrent.
	class ManagedQueue final {
	private:
		friend class QueueAllocator;
		friend class QueueReservationSession;

		std::shared_ptr<QueueAllocatorState> state;
		std::size_t slot;
		vk::SharedQueue impl;

		ManagedQueue(
			std::shared_ptr<QueueAllocatorState> const& state_,
			std::size_t slot_,
			vk::SharedQueue&& impl_
		) noexcept
			: state(state_),
			slot(slot_),
			impl(std::move(impl_)) {
		}

	public:
		ManagedQueue(ManagedQueue const&) = delete;
		ManagedQueue& operator=(ManagedQueue const&) = delete;

		ManagedQueue(ManagedQueue&& other) noexcept
			: state(std::move(other.state)),
			slot(std::exchange(other.slot, 0u)),
			impl(std::move(other.impl)) {
		}

		ManagedQueue& operator=(ManagedQueue&& other) noexcept {
			if (this != &other) {
				state = std::move(other.state);
				slot = std::exchange(other.slot, 0u);
				impl = std::move(other.impl);
			}
			return *this;
		}

		QueueIdentifier GetIdentifier() const noexcept {
			return state->queues[slot].identifier;
		}

		vk::SharedQueue GetNativeQueue() const noexcept {
			return impl;
		}

		/// Every vkQueueSubmit*, vkQueuePresentKHR and vkQueueWaitIdle call on
		/// this physical queue must hold the returned device-level mutex.
		std::mutex& GetSubmissionMutex() const noexcept {
			return state->queues[slot].submission_mutex;
		}

		/// Reports whether the given surface can be presented from this queue's
		/// family (VkGetPhysicalDeviceSurfaceSupportKHR).
		bool SupportsPresent(
			vk::SharedPhysicalDevice const& physical_device,
			vk::SharedSurfaceKHR const& surface,
			std::shared_ptr<vk::detail::DispatchLoaderDynamic> const& dispatcher
		) const {
			return physical_device->getSurfaceSupportKHR(state->queues[slot].identifier.family, *surface, *dispatcher) != 0u;
		}
	};

	/// Maps one input QueueRequest to a ManagedQueue stored by the reservation
	/// session. queue is an index rather than a pointer, so moving the session
	/// cannot invalidate assignments.
	struct QueueAssignment {
		std::size_t queue;
		std::uint64_t work;
	};

	/// Owns every physical queue selected for one graph exactly once and keeps
	/// one load reservation per input batch until submission or rollback.
	class QueueReservationSession final {
	private:
		friend class QueueAllocator;

		std::shared_ptr<QueueAllocatorState> state;
		std::vector<ManagedQueue> queues;
		std::vector<QueueAssignment> assignments;
		std::vector<bool> committed;

		QueueReservationSession(
			std::shared_ptr<QueueAllocatorState> const& state_,
			std::vector<ManagedQueue>&& queues_,
			std::vector<QueueAssignment>&& assignments_,
			std::vector<bool>&& committed_
		) noexcept
			: state(state_),
			queues(std::move(queues_)),
			assignments(std::move(assignments_)),
			committed(std::move(committed_)) {
		}

		void Rollback() noexcept {
			if (!state) {
				return;
			}
			std::unique_lock<std::mutex> lock(state->mutex);
			for (std::size_t index = 0u; index < assignments.size(); ++index) {
				if (committed[index]) {
					continue;
				}
				auto slot = queues[assignments[index].queue].slot;
				state->queues[slot].reserved_work -= assignments[index].work;
			}
			state.reset();
		}

	public:
		QueueReservationSession(QueueReservationSession const&) = delete;
		QueueReservationSession& operator=(QueueReservationSession const&) = delete;

		QueueReservationSession(QueueReservationSession&& other) noexcept
			: state(std::move(other.state)),
			queues(std::move(other.queues)),
			assignments(std::move(other.assignments)),
			committed(std::move(other.committed)) {
		}

		QueueReservationSession& operator=(QueueReservationSession&& other) noexcept {
			if (this != &other) {
				Rollback();
				queues = std::move(other.queues);
				assignments = std::move(other.assignments);
				committed = std::move(other.committed);
				state = std::move(other.state);
			}
			return *this;
		}

		~QueueReservationSession() noexcept {
			Rollback();
		}

		std::size_t GetAssignmentCount() const noexcept {
			return assignments.size();
		}

		QueueAssignment const& GetAssignment(
			std::size_t batch
		) const noexcept {
			return assignments[batch];
		}

		ManagedQueue& GetQueue(this auto&& self, std::size_t batch) noexcept {
			return self.queues[self.assignments[batch].queue];
		}

		/// Converts one batch from reserved load to submitted load. Call only
		/// after the native queue submission succeeds.
		QueueWorkToken Commit(std::size_t batch) {
			if (!state || committed[batch]) {
				throw std::logic_error("Vulkan queue assignment is not active");
			}
			auto const& assignment = assignments[batch];
			auto slot = queues[assignment.queue].slot;
			{
				std::unique_lock<std::mutex> lock(state->mutex);
				state->queues[slot].reserved_work -= assignment.work;
				state->queues[slot].submitted_work += assignment.work;
				committed[batch] = true;
			}
			return QueueWorkToken(state, slot, assignment.work);
		}
	};

	/// Builds the immutable device queue creation plan and atomically schedules
	/// complete execution graphs onto the physical queue slots it describes.
	class QueueAllocator {
	private:
		std::shared_ptr<QueueAllocatorState> state;

	public:
		QueueAllocator(std::span<CommandQueueInfo const> queue_families)
			: state(std::make_shared<QueueAllocatorState>()) {
			state->families.reserve(queue_families.size());
			std::size_t queue_count = 0u;
			for (auto const& queue_family : queue_families) {
				if (queue_family.count == 0u) {
					throw std::invalid_argument("Vulkan queue family must expose at least one queue");
				}
				if (queue_family.count >
					std::numeric_limits<std::size_t>::max() - queue_count) {
					throw std::length_error("Vulkan queue count overflow");
				}
				queue_count += queue_family.count;
				QueueFamilyState family;
				family.info = queue_family;
				family.priorities.reserve(queue_family.count);
				for (std::uint32_t index = 0u; index < queue_family.count; ++index) {
					family.priorities.emplace_back(NativePriority(index, queue_family.count));
				}
				state->families.emplace_back(std::move(family));
			}

			// Spans are built only after family storage is complete and never moves.
			state->create_plans.reserve(state->families.size());
			for (auto const& family : state->families) {
				state->create_plans.emplace_back(family.info.family, family.priorities);
				for (std::uint32_t index = 0u; index < family.info.count; ++index) {
					state->queues.emplace_back(
						QueueIdentifier(family.info.family, index),
						family.info.capabilities,
						family.priorities[index]
					);
				}
			}
		}

		std::span<QueueCreatePlan const> GetCreatePlans() const noexcept {
			return state->create_plans;
		}

		/// Assigns every request under one allocator lock. All temporary storage
		/// and SharedQueue objects are prepared before the no-throw state commit,
		/// so an exception never leaves a partially reserved graph.
		QueueReservationSession Reserve(
			vk::SharedDevice const& device,
			std::shared_ptr<vk::detail::DispatchLoaderDynamic> const& dispatcher,
			std::span<QueueRequest const> requests
		) {
			for (std::size_t index = 0u; index < requests.size(); ++index) {
				ValidateRequest(requests[index], index, requests.size());
			}

			std::vector<std::vector<std::size_t>> successors(requests.size());
			std::vector<std::size_t> indegrees(requests.size(), 0u);
			for (std::size_t index = 0u; index < requests.size(); ++index) {
				for (std::size_t dependency : requests[index].dependencies) {
					successors[dependency].emplace_back(index);
					++indegrees[index];
				}
			}

			std::vector<std::size_t> selected_slots(requests.size());
			std::vector<bool> request_assigned(requests.size(), false);
			std::vector<bool> slot_selected(state->queues.size(), false);
			std::vector<std::uint64_t> projected_work(state->queues.size(), 0u);
			std::unique_lock<std::mutex> lock(state->mutex);

			auto IsCandidate = [&](std::size_t slot, QueueRequest const& request) {
				auto const& queue = state->queues[slot];
				return SupportsCapabilities(queue, request.required_capabilities) && ContainsFamily(request.allowed_families, queue.identifier.family);
				};

			// A request's candidate count is invariant during the whole graph:
			// IsCandidate reads only request and queue capabilities/families, none of
			// which the loop below mutates. Precomputing the counts removes the
			// per-request recount the selection loop used to repeat. The zero check
			// stays inside the loop so only ready requests fail here; a request blocked
			// behind a throwing dependency is never probed.
			std::vector<std::size_t> candidate_counts(requests.size());
			for (std::size_t index = 0u; index < requests.size(); ++index) {
				for (std::size_t slot = 0u; slot < state->queues.size(); ++slot) {
					candidate_counts[index] += IsCandidate(slot, requests[index]) ? 1u : 0u;
				}
			}

			// True when lhs is the more constrained ready request: fewer candidate
			// queues, then higher priority, then more estimated work, then earlier index.
			auto IsMoreConstrained = [&](std::size_t lhs, std::size_t rhs) {
				auto left_count = candidate_counts[lhs];
				auto right_count = candidate_counts[rhs];
				if (left_count != right_count) {
					return left_count < right_count;
				}
				auto const& left = requests[lhs];
				auto const& right = requests[rhs];
				if (left.priority != right.priority) {
					return left.priority > right.priority;
				}
				if (left.estimated_work != right.estimated_work) {
					return left.estimated_work > right.estimated_work;
				}
				return lhs < rhs;
			};

			for (std::size_t assigned_count = 0u; assigned_count < requests.size(); ++assigned_count) {
				// Pick the most constrained ready request.
				std::optional<std::size_t> selected_request;
				for (std::size_t request_index = 0u;
					request_index < requests.size();
					++request_index) {
					if (request_assigned[request_index] || indegrees[request_index] != 0u) {
						continue;
					}
					if (candidate_counts[request_index] == 0u) {
						throw std::runtime_error(
							"No Vulkan queue satisfies a graph request"
						);
					}
					if (!selected_request || IsMoreConstrained(request_index, *selected_request)) {
						selected_request = request_index;
					}
				}
				if (!selected_request) {
					throw std::invalid_argument("Vulkan queue request dependencies contain a cycle");
				}

				auto request_index = *selected_request;
				auto const& request = requests[request_index];

				// Smaller tuple = better queue for this request. The champion slot's own
				// state never changes while scanning, so a running best score replaces
				// the previous per-candidate recomputation of the champion's score.
				auto Score = [&](std::size_t slot) {
					auto const& queue = state->queues[slot];
					auto cross_queue_dependencies = std::ranges::count_if(
						request.dependencies,
						[&](std::size_t dependency) {
							return selected_slots[dependency] != slot;
						}
					);
					return std::tuple(
						request.preferred_family && queue.identifier.family != *request.preferred_family,
						CapabilityCount(queue.capabilities) - CapabilityCount(request.required_capabilities),
						std::abs(queue.priority - request.priority),
						queue.reserved_work + queue.submitted_work + projected_work[slot],
						cross_queue_dependencies,
						!slot_selected[slot],
						queue.identifier.family,
						queue.identifier.index
					);
					};

				std::optional<std::size_t> selected_slot;
				std::optional<decltype(Score(0u))> best_score;
				for (std::size_t slot = 0u; slot < state->queues.size(); ++slot) {
					if (!IsCandidate(slot, request)) {
						continue;
					}
					auto score = Score(slot);
					if (!best_score || score < *best_score) {
						selected_slot = slot;
						best_score = score;
					}
				}

				selected_slots[request_index] = *selected_slot;
				slot_selected[*selected_slot] = true;
				auto work = NormalizedWork(request.estimated_work);
				auto const& queue = state->queues[*selected_slot];
				if (work > std::numeric_limits<std::uint64_t>::max() -
					queue.reserved_work -
					queue.submitted_work -
					projected_work[*selected_slot]) {
					throw std::overflow_error("Vulkan queue work estimate overflow");
				}
				projected_work[*selected_slot] += work;
				request_assigned[request_index] = true;
				for (std::size_t successor : successors[request_index]) {
					--indegrees[successor];
				}
			}

			std::vector<std::size_t> slot_to_managed(
				state->queues.size(),
				std::numeric_limits<std::size_t>::max()
			);
			std::vector<ManagedQueue> managed_queues;
			managed_queues.reserve(std::ranges::count(slot_selected, true));
			for (std::size_t slot = 0u; slot < state->queues.size(); ++slot) {
				if (!slot_selected[slot]) {
					continue;
				}
				auto identifier = state->queues[slot].identifier;
				vk::Queue native_queue = device->getQueue(
					identifier.family,
					identifier.index,
					*dispatcher
				);
				slot_to_managed[slot] = managed_queues.size();
				managed_queues.push_back(
					ManagedQueue(
						state,
						slot,
						vk::SharedQueue(native_queue, device)
					)
				);
			}

			std::vector<QueueAssignment> assignments;
			assignments.reserve(requests.size());
			for (std::size_t index = 0u; index < requests.size(); ++index) {
				assignments.emplace_back(
					slot_to_managed[selected_slots[index]],
					NormalizedWork(requests[index].estimated_work)
				);
			}
			std::vector<bool> committed(requests.size(), false);
			QueueReservationSession result(
				state,
				std::move(managed_queues),
				std::move(assignments),
				std::move(committed)
			);

			// This is the only allocator-state mutation in Reserve. Everything that
			// can allocate or query Vulkan has already completed successfully.
			for (std::size_t index = 0u; index < requests.size(); ++index) {
				auto slot = selected_slots[index];
				auto work = NormalizedWork(requests[index].estimated_work);
				state->queues[slot].reserved_work += work;
			}
			return result;
		}

		bool Supports(CommandQueueType capabilities) const noexcept {
			if (capabilities == CommandQueueType::None) {
				return false;
			}
			return std::ranges::any_of(
				state->queues,
				[capabilities](QueueState const& queue) {
					return SupportsCapabilities(queue, capabilities);
				}
			);
		}
	};

}
#endif // !defined(__APPLE__)
