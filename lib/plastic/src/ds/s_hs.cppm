module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <initializer_list>
#include <array>
#include <functional>
#include <optional>
#include <string_view>
#include <compare>
#include <concepts>
#include <ranges>
#endif // !defined(__cpp_lib_modules)
export module plastic.static_hash_set;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

import plastic.static_hash_table;

namespace plastic::ds {
	export template <
		class Key, std::size_t TableSize,
		class Hash = ConstexprHasher<Key>,
		class Equal = std::equal_to<Key>
	> class StaticHashSet {
	public:
		using key_type = Key;
		using value_type = Key;
		using size_type = std::size_t;
		using difference_type = std::ptrdiff_t;
		using hasher = Hash;
		using key_equal = Equal;

	private:
		enum class SlotState : std::uint8_t { EMPTY, OCCUPIED, TOMBSTONE };

		struct Slot {
			SlotState state = SlotState::EMPTY;
			std::optional<key_type> key;   // valid only when state == OCCUPIED
		};

		std::array<Slot, TableSize> m_slots{};
		size_type m_size = 0;

#if defined(_MSVC_LANG)
		[[msvc::no_unique_address]]
#else
		[[no_unique_address]]
#endif
		Hash m_hasher;

#if defined(_MSVC_LANG)
		[[msvc::no_unique_address]]
#else
		[[no_unique_address]]
#endif
		Equal m_equal;

	public:
		template <bool IsConst>
		class BasicIterator {
			friend class StaticHashSet;
		private:
			using SlotPtr = std::conditional_t<IsConst, Slot const*, Slot*>;
			SlotPtr m_ptr = nullptr;
			Slot const* m_end = nullptr;

			constexpr BasicIterator(SlotPtr ptr, Slot const* end) noexcept
				: m_ptr(ptr), m_end(end) {}

			constexpr void AdvanceToNextOccupied() noexcept {
				if (!m_ptr) return;
				++m_ptr;
				while (m_ptr != m_end && m_ptr->state != SlotState::OCCUPIED)
					++m_ptr;
				if (m_ptr == m_end) m_ptr = nullptr;
			}

		public:
			using iterator_category = std::forward_iterator_tag;
			using value_type = Key;
			using difference_type = std::ptrdiff_t;
			using pointer = std::conditional_t<IsConst, Key const*, Key*>;
			using reference = std::conditional_t<IsConst, Key const&, Key&>;

			constexpr BasicIterator() = default;
			constexpr BasicIterator(BasicIterator const&) = default;
			constexpr BasicIterator& operator=(BasicIterator const&) = default;

			template <bool OtherConst, class = std::enable_if_t<IsConst && !OtherConst>>
			constexpr BasicIterator(BasicIterator<OtherConst> const& other) noexcept
				: m_ptr(other.m_ptr), m_end(other.m_end) {}

			constexpr reference operator*() const noexcept {
				return *m_ptr->key;
			}
			constexpr pointer operator->() const noexcept {
				return &(*m_ptr->key);
			}

			constexpr BasicIterator& operator++() noexcept {
				AdvanceToNextOccupied();
				return *this;
			}
			constexpr BasicIterator operator++(int) noexcept {
				BasicIterator tmp = *this;
				++(*this);
				return tmp;
			}

			friend constexpr std::strong_ordering operator<=>(BasicIterator const& a, BasicIterator const& b) noexcept = default;
		};

		using iterator = BasicIterator<false>;
		using const_iterator = BasicIterator<true>;

	private:
		constexpr size_type NextProbe(size_type index) const noexcept {
			return (index + 1) % TableSize;
		}

		constexpr size_type FindIndex(key_type const& key) const noexcept {
			size_type h = m_hasher(key) % TableSize;
			size_type i = h;
			while (m_slots[i].state != SlotState::EMPTY) {
				if (m_slots[i].state == SlotState::OCCUPIED && m_equal(*m_slots[i].key, key))
					return i;
				i = NextProbe(i);
				if (i == h) break;
			}
			return TableSize;
		}

		template <class K>
		constexpr std::pair<iterator, bool> InsertImpl(K&& key) {
			size_type h = m_hasher(key) % TableSize;
			size_type i = h;
			size_type first_tombstone = TableSize;

			while (m_slots[i].state != SlotState::EMPTY) {
				if (m_slots[i].state == SlotState::OCCUPIED && m_equal(*m_slots[i].key, key)) {
					return { iterator(&m_slots[i], m_slots.data() + TableSize), false };
				}
				if (m_slots[i].state == SlotState::TOMBSTONE && first_tombstone == TableSize) {
					first_tombstone = i;
				}
				i = NextProbe(i);
				if (i == h) {
					if (first_tombstone != TableSize) {
						Slot& target = m_slots[first_tombstone];
						target.state = SlotState::OCCUPIED;
						target.key.emplace(std::forward<K>(key));
						++m_size;
						return { iterator(&target, m_slots.data() + TableSize), true };
					}
					return { end(), false };
				}
			}
			size_type const target_idx = (first_tombstone != TableSize) ? first_tombstone : i;
			Slot& target = m_slots[target_idx];
			target.state = SlotState::OCCUPIED;
			target.key.emplace(std::forward<K>(key));
			++m_size;
			return { iterator(&target, m_slots.data() + TableSize), true };
		}

	public:

		constexpr StaticHashSet() = default;
		constexpr StaticHashSet(Hash const& hf, Equal const& eqf)
			: m_hasher(hf), m_equal(eqf) {}

		template <std::size_t N>
		constexpr StaticHashSet(Key const (&arr)[N], Hash const& hf = {}, Equal const& eqf = {})
			: m_hasher(hf), m_equal(eqf) {
			static_assert(N <= TableSize,
				"StaticHashSet: array size exceeds fixed capacity");
			for (auto const& k : arr) {
				auto result = InsertImpl(k);
				if (result.first == end()) {
					throw std::length_error(
						"StaticHashSet: insertion failed (should not happen)");
				}
			}
		}


		constexpr size_type size() const noexcept { return m_size; }
		constexpr bool empty() const noexcept { return m_size == 0; }
		static constexpr size_type max_size() noexcept { return TableSize; }
		static constexpr size_type capacity() noexcept { return TableSize; }

		constexpr iterator begin() noexcept {
			for (size_type i = 0; i < TableSize; ++i) {
				if (m_slots[i].state == SlotState::OCCUPIED) {
					return iterator(&m_slots[i], m_slots.data() + TableSize);
				}
			}
			return end();
		}
		constexpr const_iterator begin() const noexcept {
			for (size_type i = 0; i < TableSize; ++i) {
				if (m_slots[i].state == SlotState::OCCUPIED) {
					return const_iterator(&m_slots[i], m_slots.data() + TableSize);
				}
			}
			return end();
		}
		constexpr const_iterator cbegin() const noexcept { return begin(); }

		constexpr iterator end() noexcept {
			return iterator(nullptr, m_slots.data() + TableSize);
		}
		constexpr const_iterator end() const noexcept {
			return const_iterator(nullptr, m_slots.data() + TableSize);
		}
		constexpr const_iterator cend() const noexcept { return end(); }

		constexpr iterator find(key_type const& key) noexcept {
			size_type const idx = FindIndex(key);
			if (idx == TableSize) return end();
			return iterator(&m_slots[idx], m_slots.data() + TableSize);
		}
		constexpr const_iterator find(key_type const& key) const noexcept {
			size_type const idx = FindIndex(key);
			if (idx == TableSize) return end();
			return const_iterator(&m_slots[idx], m_slots.data() + TableSize);
		}

		constexpr size_type count(key_type const& key) const noexcept {
			return find(key) != end() ? 1 : 0;
		}
		constexpr bool contains(key_type const& key) const noexcept {
			return find(key) != end();
		}

		constexpr std::pair<iterator, bool> insert(key_type const& key) {
			return InsertImpl(key);
		}
		constexpr std::pair<iterator, bool> insert(key_type&& key) {
			return InsertImpl(std::move(key));
		}

		template <class... Args>
		constexpr std::pair<iterator, bool> emplace(Args&&... args) {
			key_type key(std::forward<Args>(args)...);
			return insert(std::move(key));
		}

		constexpr size_type erase(key_type const& key) noexcept {
			size_type const idx = FindIndex(key);
			if (idx == TableSize) {
				return 0;
			}
			Slot& slot = m_slots[idx];
			slot.state = SlotState::TOMBSTONE;
			slot.key.reset();
			--m_size;
			return 1;
		}

		constexpr iterator erase(iterator pos) noexcept {
			if (pos == end()) {
				return end();
			}
			Slot* slot = const_cast<Slot*>(pos.m_ptr);
			slot->state = SlotState::TOMBSTONE;
			slot->key.reset();
			--m_size;
			iterator next = pos;
			++next;
			return next;
		}

		constexpr iterator erase(const_iterator pos) noexcept {
			return erase(iterator(const_cast<Slot*>(pos.m_ptr), const_cast<Slot*>(pos.m_end)));
		}

		constexpr void clear() noexcept {
			for (auto& slot : m_slots) {
				if (slot.state == SlotState::OCCUPIED) {
					slot.key.reset();
				}
				slot.state = SlotState::EMPTY;
			}
			m_size = 0;
		}

		constexpr hasher hash_function() const { return m_hasher; }
		constexpr key_equal key_eq() const { return m_equal; }
	};

}
