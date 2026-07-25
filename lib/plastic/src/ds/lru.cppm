module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <array>
#include <functional>
#include <optional>
#include <string_view>
#include <compare>
#include <concepts>
#endif // !defined(__cpp_lib_modules)
export module plastic.lru;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace {
	template <class H>
	concept HashTableConcept = requires(H h, typename H::key_type const& k, typename H::mapped_type const& v) {
		typename H::key_type;
		typename H::mapped_type;
		{ h.find(k) } -> std::same_as<typename H::iterator>;
		{ h.end() } -> std::same_as<typename H::iterator>;
		{ h.erase(k) } -> std::same_as<typename H::size_type>;
		{ h.insert(std::pair<typename H::key_type const, typename H::mapped_type>{k, v}) } -> std::same_as<std::pair<typename H::iterator, bool>>;
	};

	template <class L>
	concept ListConcept = requires(L l, typename L::iterator it, typename L::const_iterator cit, typename L::value_type const& val) {
		typename L::iterator;
		typename L::const_iterator;
		typename L::value_type;
		{ l.push_front(val) };
		{ l.pop_back() };
		{ l.splice(l.begin(), l, it) };
		{ l.erase(it) } -> std::same_as<typename L::iterator>;
		{ l.begin() } -> std::same_as<typename L::iterator>;
		{ l.end() } -> std::same_as<typename L::iterator>;
		{ l.cbegin() } -> std::same_as<typename L::const_iterator>;
		{ l.cend() } -> std::same_as<typename L::const_iterator>;
		{ l.empty() } -> std::same_as<bool>;
		{ l.size() } -> std::same_as<typename L::size_type>;
	};

}

namespace plastic::ds {

	export template <HashTableConcept HashTable, ListConcept List, std::size_t Capacity> class LRUCache {
	public:
		using key_type = typename HashTable::key_type;
		using value_type = typename List::value_type;
		using mapped_type = typename HashTable::mapped_type;
		using iterator = typename List::iterator;
		using const_iterator = typename List::const_iterator;
		using size_type = std::size_t;

		static_assert(std::same_as<typename value_type::first_type, key_type const>,
			"List::value_type must be std::pair<Key const, Value>");

		static_assert(std::same_as<typename HashTable::mapped_type, iterator>,
			"HashTable::mapped_type must be List::iterator");

		static_assert(std::is_base_of_v<std::bidirectional_iterator_tag,
			typename std::iterator_traits<iterator>::iterator_category>,
			"List::iterator must be bidirectional");

	private:
		struct AlwaysEvict {
			bool operator()(value_type const&) const noexcept {
				return true;
			}
		};

		List m_list;			// stores key-value pairs, front = most recent
		HashTable m_map;		// maps key -> iterator into m_list

	public:
		typename value_type::second_type& Get(key_type const& key) {
			auto it = m_map.find(key);
			if (it == m_map.end()) {
				throw std::out_of_range("LRU::Get: key not found");
			}
			// Move the node to the front of the list
			m_list.splice(m_list.begin(), m_list, it->second);
			return it->second->second;
		}

		template <class CanEvict>
		bool TryPut(
			key_type const& key,
			typename value_type::second_type const& value,
			CanEvict const& can_evict,
			bool allow_overflow = false
		) {
			auto it = m_map.find(key);
			if (it != m_map.end()) {
				// Update existing value and move to front
				it->second->second = value;
				m_list.splice(m_list.begin(), m_list, it->second);
				return true;
			}

			// Evict the least recently used element if at capacity
			if (m_list.size() >= Capacity) {
				auto candidate = m_list.end();
				for (auto current = m_list.begin(); current != m_list.end(); ++current) {
					if (can_evict(*current)) {
						candidate = current;
					}
				}
				if (candidate == m_list.end() && !allow_overflow) {
					return false;
				}
				if (candidate != m_list.end()) {
					m_map.erase(candidate->first);
					m_list.erase(candidate);
				}
			}

			// Insert new element at the front
			m_list.push_front({ key, value });
			m_map[key] = m_list.begin();
			return true;
		}

		void Put(key_type const& key, typename value_type::second_type const& value) {
			TryPut(key, value, AlwaysEvict{});
		}

		bool Contains(key_type const& key) const {
			return m_map.find(key) != m_map.end();
		}

		bool Erase(key_type const& key) {
			auto it = m_map.find(key);
			if (it == m_map.end()) {
				return false;
			}
			m_list.erase(it->second);
			m_map.erase(it);
			return true;
		}

		void Clear() {
			m_list.clear();
			m_map.clear();
		}

		size_type Size() const {
			return m_list.size();
		}

		bool Empty() const {
			return m_list.empty();
		}

		iterator begin() {
			return m_list.begin();
		}

		const_iterator begin() const {
			return m_list.begin();
		}

		iterator end() {
			return m_list.end();
		}

		const_iterator end() const {
			return m_list.end();
		}

		const_iterator cbegin() const {
			return m_list.cbegin();
		}

		const_iterator cend() const {
			return m_list.cend();
		}

	};

} // namespace plastic::ds
