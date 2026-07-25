module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <memory>
#include <random>
#include <array>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <span>
#include <limits>
#endif
module fyuu_rhi:vulkan_queue_allocator;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif

namespace fyuu_rhi::vulkan {

	enum class CommandQueueType : std::uint8_t {
		None = 0u,
		Graphics = 1u << 0u,
		Compute = 1u << 1u,
		Copy = 1u << 2u,
	};

	[[nodiscard]] constexpr CommandQueueType operator|(CommandQueueType lhs, CommandQueueType rhs) noexcept {
		return static_cast<CommandQueueType>(
			static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs)
		);
	}

	[[nodiscard]] constexpr CommandQueueType operator&(CommandQueueType lhs, CommandQueueType rhs) noexcept {
		return static_cast<CommandQueueType>(
			static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs)
		);
	}

	constexpr CommandQueueType& operator|=(CommandQueueType& lhs, CommandQueueType rhs) noexcept {
		return lhs = lhs | rhs;
	}

	constexpr CommandQueueType& operator&=(CommandQueueType& lhs, CommandQueueType rhs) noexcept {
		return lhs = lhs & rhs;
	}

	enum class QueuePriority : std::uint8_t {
		High,
		Medium,
		Low
	};

	struct CommandQueueInfo {
		CommandQueueType type;
		std::optional<std::uint32_t> family;
		std::uint32_t num_available;
	};

}

namespace {

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::vulkan;

	struct QueueSet {
		CommandQueueInfo info;
		std::vector<float> priorities;

		std::unordered_set<std::uint32_t> allocated_queue; // Indices that are currently in use
		std::mutex allocated_queue_mutex;

		QueueSet(CommandQueueInfo const& info_, std::span<float const> priorities_)
			: info(info_),
			priorities(priorities_.begin(), priorities_.end()),
			allocated_queue(),
			allocated_queue_mutex() {

		}
	};

}

namespace fyuu_rhi::vulkan {

	struct AllocatedCommandQueueInfo {
		std::uint32_t family;
		std::uint32_t index;
	};

	// Configuration options for the queue allocator.
	struct QueueOptions {
		std::size_t max_graphics; // Max number of graphics queues to use
		std::span<float const> graphics_priorities; // Priorities for each graphics queue

		std::size_t max_compute; // Max number of compute queues
		std::span<float const> compute_priorities; // Priorities for compute queues

		std::size_t max_copy; // Max number of copy queues
		std::span<float const> copy_priorities; // Priorities for copy queues

		static QueueOptions PlatformDefault(); // Returns a default configuration for the current platform
	};

	// Move-only lease that returns its queue index to the allocator on destruction.
	class ManagedQueue final {
	private:
		std::shared_ptr<QueueSet> m_queue_set;
		std::uint32_t m_index;

	public:
		ManagedQueue(std::shared_ptr<QueueSet> const& queue_set, std::uint32_t index) noexcept
			: m_queue_set(queue_set), m_index(index) {
		}

		~ManagedQueue() {
			if (m_queue_set) {
				std::unique_lock<std::mutex> lock(m_queue_set->allocated_queue_mutex);
				m_queue_set->allocated_queue.erase(m_index);
			}
		}

		// Queue leases have unique ownership.
		ManagedQueue(ManagedQueue const&) = delete;
		ManagedQueue& operator=(ManagedQueue const&) = delete;

		ManagedQueue(ManagedQueue&& other) noexcept
			: m_queue_set(std::move(other.m_queue_set)),
			m_index(other.m_index) {

		}

		ManagedQueue& operator=(ManagedQueue&& other) noexcept {
			std::swap(m_queue_set, other.m_queue_set);
			std::swap(m_index, other.m_index);
			return *this;
		}

		[[nodiscard]] std::uint32_t GetFamily() const noexcept {
			return *m_queue_set->info.family;
		}

		[[nodiscard]] std::uint32_t GetIndex() const noexcept {
			return m_index;
		}

		[[nodiscard]] AllocatedCommandQueueInfo GetInfo() const noexcept {
			return { GetFamily(), m_index };
		}
	};

	class QueueAllocator {
	private:
		std::unordered_map<CommandQueueType, std::shared_ptr<QueueSet>> m_queue_sets;

		static std::shared_ptr<QueueSet> MakeQueueSet(CommandQueueType type, std::span<CommandQueueInfo const> queue_infos, QueueOptions const& init_options);

	public:
		// Builds queue sets for every capability available in the given family list.
		QueueAllocator(
			std::span<CommandQueueInfo const> queue_infos,
			QueueOptions const& init_options = QueueOptions::PlatformDefault()
		);

		// Allocate a queue of the given type with the requested priority.
		[[nodiscard]] ManagedQueue Allocate(CommandQueueType type, QueuePriority priority);
		[[nodiscard]] bool Supports(CommandQueueType type) const noexcept;

		// Query properties of the underlying queue set for a given type.
		[[nodiscard]] std::uint32_t GetFamily(CommandQueueType type) const noexcept;
		[[nodiscard]] std::uint32_t GetTotalQueue(CommandQueueType type) const noexcept;
		[[nodiscard]] std::span<float const> GetPriorities(CommandQueueType type) const noexcept;
	};

}

namespace fyuu_rhi::vulkan {

	QueueOptions QueueOptions::PlatformDefault() {

		QueueOptions options;
#if defined(_WIN32) || defined(__linux__)
		// high performance desktop GPUs
		options.max_graphics = 3u;
		options.max_compute = 3u;
		options.max_copy = 3u;

		// high, medium, low
		static constexpr std::array priorities = { 1.00f, 0.33f, 0.67f };

		options.graphics_priorities = priorities;
		options.compute_priorities = priorities;
		options.copy_priorities = priorities;
#elif defined(__ANDROID__)
		// mobile and embedded devices
		options.max_graphics = 1u;
		options.max_compute = 1u;
		options.max_copy = 1u;

		static constexpr std::array priorities = { 1.00f };

		options.graphics_priorities = priorities;
		options.compute_priorities = priorities;
		options.copy_priorities = priorities;
#endif // defined(_WIN32) || defined(__linux__)
		return options;
	}

	std::shared_ptr<QueueSet> QueueAllocator::MakeQueueSet(CommandQueueType type, std::span<CommandQueueInfo const> queue_infos, QueueOptions const& init_options) {
		std::vector<std::uint32_t> indices;
		std::uint8_t minimum_capability_count = std::numeric_limits<std::uint8_t>::max();
	
		auto length = static_cast<std::uint32_t>(queue_infos.size());

		// Collect indices of queue families that support the requested type.

		for (std::uint32_t i = 0; i < length; ++i) {
			if ((queue_infos[i].type & type) == type) {
				auto capabilities = static_cast<std::uint8_t>(queue_infos[i].type);
				std::uint8_t capability_count = 0u;
				while (capabilities != 0u) {
					capability_count += capabilities & 1u;
					capabilities >>= 1u;
				}

				if (capability_count < minimum_capability_count) {
					indices.clear();
					minimum_capability_count = capability_count;
				}
				if (capability_count == minimum_capability_count) {
					indices.emplace_back(i);
				}
			}
		}

		if (indices.empty()) {
			return nullptr;
		}

		// Randomly select one family from the matching ones.

		static thread_local std::mt19937 gen(std::random_device{}());
		static thread_local std::uniform_int_distribution<std::size_t> dist;
		using param_t = std::uniform_int_distribution<std::size_t>::param_type;
		dist.param(param_t{ 0, indices.size() - 1 });

		std::size_t selected_index = dist(gen);

		CommandQueueInfo queue_info = queue_infos[indices[selected_index]];

		// Cap the number of queues to use according to init_options and assign priorities.
		switch (type) {
		case CommandQueueType::Copy:
			queue_info.num_available = std::min(
				queue_info.num_available,
				static_cast<std::uint32_t>(init_options.max_copy)
			);
			return std::make_shared<QueueSet>(queue_info, init_options.copy_priorities.subspan(0, queue_info.num_available));

		case CommandQueueType::Compute:
			queue_info.num_available = std::min(
				queue_info.num_available,
				static_cast<std::uint32_t>(init_options.max_compute)
			);
			return std::make_shared<QueueSet>(queue_info, init_options.compute_priorities.subspan(0, queue_info.num_available));

		case CommandQueueType::Graphics:
		default:
			queue_info.num_available = std::min(
				queue_info.num_available,
				static_cast<std::uint32_t>(init_options.max_graphics)
			);
			return std::make_shared<QueueSet>(queue_info, init_options.graphics_priorities.subspan(0, queue_info.num_available));
		}
	}

	QueueAllocator::QueueAllocator(
		std::span<CommandQueueInfo const> queue_infos,
		QueueOptions const& init_options
	) : m_queue_sets() {
		for (CommandQueueType type : {
			CommandQueueType::Graphics,
			CommandQueueType::Compute,
			CommandQueueType::Copy
		}) {
			auto queue_set = MakeQueueSet(type, queue_infos, init_options);
			if (queue_set) {
				m_queue_sets.emplace(type, std::move(queue_set));
			}
		}

	}

	ManagedQueue QueueAllocator::Allocate(CommandQueueType type, QueuePriority priority) {

		auto& queue_set = m_queue_sets.at(type);
		auto const& priorities = queue_set->priorities;
		std::vector<std::uint32_t> candidates;

		auto length = static_cast<std::uint32_t>(priorities.size());
		switch (priority) {
		case QueuePriority::Low:
			for (std::uint32_t i = 0; i < length; ++i)
				if (priorities[i] <= 0.33f) candidates.emplace_back(i);
			break;
		case QueuePriority::Medium:
			for (std::uint32_t i = 0; i < length; ++i)
				if (priorities[i] > 0.33f && priorities[i] <= 0.67f) candidates.emplace_back(i);
			break;
		case QueuePriority::High:
		default:
			for (std::uint32_t i = 0; i < length; ++i)
				if (priorities[i] > 0.67f) candidates.emplace_back(i);
			break;
		}

		if (candidates.empty())
			throw std::runtime_error("No queue satisfies the priorities");

		std::unique_lock<std::mutex> lock(queue_set->allocated_queue_mutex);
		for (std::uint32_t index : candidates) {
			auto& allocated_queue = queue_set->allocated_queue;
			if (allocated_queue.find(index) == allocated_queue.end()) {
				allocated_queue.insert(index);
				return ManagedQueue(queue_set, index);
			}
		}

		throw std::runtime_error("No queue available");
	}

	bool QueueAllocator::Supports(CommandQueueType type) const noexcept {
		return m_queue_sets.contains(type);
	}

	std::uint32_t QueueAllocator::GetFamily(CommandQueueType type) const noexcept {
		auto& qs = m_queue_sets.at(type);
		return *qs->info.family;
	}

	std::uint32_t QueueAllocator::GetTotalQueue(CommandQueueType type) const noexcept {
		return m_queue_sets.at(type)->info.num_available;
	}

	std::span<float const> QueueAllocator::GetPriorities(CommandQueueType type) const noexcept {
		return m_queue_sets.at(type)->priorities;
	}

}

#endif // !defined(__APPLE__)
