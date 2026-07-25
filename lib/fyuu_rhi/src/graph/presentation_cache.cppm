module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <atomic>
#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:presentation_cache;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import plastic.lru;

namespace fyuu_rhi::execution {
	template <class Pointer>
	std::size_t HashNativePointer(Pointer pointer) noexcept {
		return std::hash<void const*>{}(pointer);
	}

	std::size_t CombineHashes(std::size_t first, std::size_t second) noexcept {
		return first ^ (second + 0x9e3779b9u + (first << 6u) + (first >> 2u));
	}

	template <class Key, class Value, class Hash, std::size_t Capacity = 32u>
	class PresentationCache {
	private:
		struct Generation {
			Value value;
			std::uint64_t id;

			Generation(Value&& value_, std::uint64_t id_)
				: value(std::move(value_)), id(id_) {

			}
		};

		struct Entry {
			std::shared_ptr<Generation> current;
			std::atomic_uint32_t active_leases = 0u;
			std::uint64_t next_generation = 2u;
			std::mutex mutex;

			explicit Entry(Value&& value_)
				: current(std::make_shared<Generation>(std::move(value_), 1u)) {

			}

			[[nodiscard]] std::shared_ptr<Generation> Current() {
				std::unique_lock<std::mutex> lock(mutex);
				return current;
			}

			void Replace(Value&& value) {
				std::unique_lock<std::mutex> lock(mutex);
				current = std::make_shared<Generation>(std::move(value), next_generation++);
			}
		};

		using EntryPointer = std::shared_ptr<Entry>;
		using List = std::list<std::pair<Key const, EntryPointer>>;
		using Table = std::unordered_map<Key, typename List::iterator, Hash>;
		using Cache = plastic::ds::LRUCache<Table, List, Capacity>;

		struct CanEvict {
			bool operator()(typename Cache::value_type const& value) const noexcept {
				return value.second->active_leases.load(std::memory_order_acquire) == 0u;
			}
		};

		Cache m_cache;
		std::mutex m_mutex;

	public:
		class Lease {
		private:
			friend class PresentationCache;

			EntryPointer m_entry;
			std::shared_ptr<Generation> m_generation;

		public:
			explicit Lease(EntryPointer const& entry)
				: m_entry(entry),
				m_generation(entry->Current()) {
				m_entry->active_leases.fetch_add(1u, std::memory_order::acq_rel);
			}

			Lease(Lease const&) = delete;
			Lease& operator=(Lease const&) = delete;
			Lease(Lease&& other) noexcept
				: m_entry(std::move(other.m_entry)),
				m_generation(std::move(other.m_generation)) {

			}
			Lease& operator=(Lease&& other) noexcept {
				std::swap(m_entry, other.m_entry);
				std::swap(m_generation, other.m_generation);
				return *this;
			}

			~Lease() noexcept {
				if (m_entry) {
					m_entry->active_leases.fetch_sub(1u, std::memory_order::acq_rel);
				}
			}

			[[nodiscard]] Value& Get() noexcept {
				return m_generation->value;
			}

			[[nodiscard]] Value const& Get() const noexcept {
				return m_generation->value;
			}

			[[nodiscard]] std::uint64_t Generation() const noexcept {
				return m_generation->id;
			}
		};

		template <class Create>
		[[nodiscard]] Lease Acquire(Key const& key, Create const& create) {
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				if (m_cache.Contains(key)) {
					return Lease(m_cache.Get(key));
				}
			}

			auto created = std::make_shared<Entry>(create());
		std::unique_lock<std::mutex> lock(m_mutex);
		if (m_cache.Contains(key)) {
			return Lease(m_cache.Get(key));
		}
		m_cache.TryPut(key, created, CanEvict{}, true);
		return Lease(m_cache.Get(key));
	}

		template <class Recreate>
		void Recreate(Lease const& lease, Recreate const& recreate) {
			auto value = recreate(lease.Get());
			lease.m_entry->Replace(std::move(value));
		}
	};

}
