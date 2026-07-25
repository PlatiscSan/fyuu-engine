module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <iterator>
#include <type_traits>
#include <optional>
#include <array>
#include <concepts>
#include <compare>
#endif // !defined(__cpp_lib_modules)
export module plastic.static_list;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace plastic::ds {

	export template <class T, std::size_t Capacity> class StaticList {
	public:
		 using value_type = T;
		 using reference = value_type&;
		 using const_reference = value_type const&;
		 using size_type = std::size_t;
		 using difference_type = std::ptrdiff_t;

	private:
		struct Node {
			std::optional<T> value;
			std::size_t prev = Capacity;	// previous index, Capacity means end
			std::size_t next = Capacity;	// next index, Capacity means end
		};

		std::array<Node, Capacity> m_nodes{};
		std::size_t m_head = Capacity;	// index of first node
		std::size_t m_tail = Capacity;	// index of last node
		std::size_t m_free_head = 0;	// head of free list
		std::size_t m_size = 0;

		// internal helpers (PascalCase, same style as hash table)
		constexpr void InitFreeList() noexcept {
			for (std::size_t i = 0; i < Capacity - 1; ++i) {
				m_nodes[i].next = i + 1;
			}
			if constexpr (Capacity > 0) {
				m_nodes[Capacity - 1].next = Capacity;
			}
			m_free_head = 0;
		}

		constexpr std::size_t AllocateNode() noexcept {
			if (m_free_head == Capacity) return Capacity;
			std::size_t idx = m_free_head;
			m_free_head = m_nodes[idx].next;
			return idx;
		}

		constexpr void DeallocateNode(std::size_t idx) noexcept {
			m_nodes[idx].value.reset();
			m_nodes[idx].next = m_free_head;
			m_free_head = idx;
		}

		// Insert a node after a given index (prev_idx == Capacity means insert at head)
		template <class... Args>
		constexpr std::size_t InsertAfter(std::size_t prev_idx, Args&&... args) {
			std::size_t new_idx = AllocateNode();
			if (new_idx == Capacity) return Capacity;
			m_nodes[new_idx].value.emplace(std::forward<Args>(args)...);
			if (prev_idx == Capacity) {
				// empty list
				m_nodes[new_idx].prev = Capacity;
				m_nodes[new_idx].next = Capacity;
				m_head = new_idx;
				m_tail = new_idx;
			}
			else {
				std::size_t next_idx = m_nodes[prev_idx].next;
				m_nodes[new_idx].prev = prev_idx;
				m_nodes[new_idx].next = next_idx;
				m_nodes[prev_idx].next = new_idx;
				if (next_idx != Capacity) {
					m_nodes[next_idx].prev = new_idx;
				}
				else {
					m_tail = new_idx;
				}
			}
			++m_size;
			return new_idx;
		}

		constexpr void EraseNode(std::size_t idx) noexcept {
			std::size_t prev_idx = m_nodes[idx].prev;
			std::size_t next_idx = m_nodes[idx].next;
			if (prev_idx != Capacity) {
				m_nodes[prev_idx].next = next_idx;
			}
			else {
				m_head = next_idx;
			}
			if (next_idx != Capacity) {
				m_nodes[next_idx].prev = prev_idx;
			}
			else {
				m_tail = prev_idx;
			}
			DeallocateNode(idx);
			--m_size;
		}

		constexpr std::pair<std::size_t, std::size_t> RemoveNode(std::size_t idx) noexcept {
			std::size_t prev = m_nodes[idx].prev;
			std::size_t next = m_nodes[idx].next;
			if (prev != Capacity) {
				m_nodes[prev].next = next;
			}
			else {
				m_head = next;
			}
			if (next != Capacity) {
				m_nodes[next].prev = prev;
			}
			else {
				m_tail = prev;
			}
			m_nodes[idx].prev = Capacity;
			m_nodes[idx].next = Capacity;
			return { prev, next };
		}

		constexpr void InsertNodeAfter(std::size_t idx, std::size_t prev_idx) noexcept {
			if (prev_idx == Capacity) {
				m_nodes[idx].prev = Capacity;
				m_nodes[idx].next = m_head;
				if (m_head != Capacity) {
					m_nodes[m_head].prev = idx;
				}
				else {
					m_tail = idx;
				}
				m_head = idx;
			}
			else {
				std::size_t next_idx = m_nodes[prev_idx].next;
				m_nodes[idx].prev = prev_idx;
				m_nodes[idx].next = next_idx;
				m_nodes[prev_idx].next = idx;
				if (next_idx != Capacity) {
					m_nodes[next_idx].prev = idx;
				}
				else {
					m_tail = idx;
				}
			}
			++m_size;
		}

		constexpr void LinkChainAfter(std::size_t chain_head, std::size_t chain_tail,
			std::size_t chain_size, std::size_t prev_idx) noexcept {
			if (chain_size == 0) return;
			std::size_t next_idx = (prev_idx == Capacity) ? m_head : m_nodes[prev_idx].next;
			if (prev_idx == Capacity) {
				m_head = chain_head;
			}
			else {
				m_nodes[prev_idx].next = chain_head;
				m_nodes[chain_head].prev = prev_idx;
			}
			if (next_idx == Capacity) {
				m_tail = chain_tail;
			}
			else {
				m_nodes[chain_tail].next = next_idx;
				m_nodes[next_idx].prev = chain_tail;
			}
			m_size += chain_size;
		}

	public:
		// iterator definition
		template <bool IsConst>
		class BasicIterator {
			friend class StaticList;
		private:
			using NodePtr = std::conditional_t<IsConst, Node const*, Node*>;
			NodePtr m_ptr = nullptr;

			using OwnerPtr = std::conditional_t<IsConst, StaticList const*, StaticList*>;
			OwnerPtr m_owner = nullptr;

		public:
			using iterator_category = std::bidirectional_iterator_tag;
			using value_type = T;
			using difference_type = std::ptrdiff_t;
			using pointer = std::conditional_t<IsConst, T const*, T*>;
			using reference = std::conditional_t<IsConst, T const&, T&>;

			constexpr BasicIterator() = default;
			constexpr BasicIterator(BasicIterator const&) = default;
			constexpr BasicIterator& operator=(BasicIterator const&) = default;

			constexpr BasicIterator(NodePtr ptr, OwnerPtr owner) noexcept
				: m_ptr(ptr), m_owner(owner) {}

			template <bool OtherConst, class = std::enable_if_t<IsConst && !OtherConst>>
			constexpr BasicIterator(BasicIterator<OtherConst> const& other) noexcept
				: m_ptr(other.m_ptr), m_owner(other.m_owner) {}

			constexpr reference operator*() const noexcept {
				return *m_ptr->value;
			}
			constexpr pointer operator->() const noexcept {
				return &(*m_ptr->value);
			}

			constexpr BasicIterator& operator++() noexcept {
				if (m_ptr && m_ptr->next != m_owner->capacity()) {
					m_ptr = &m_owner->m_nodes[m_ptr->next];
				}
				else {
					m_ptr = nullptr;
				}
				return *this;
			}
			constexpr BasicIterator operator++(int) noexcept {
				BasicIterator tmp = *this;
				++(*this);
				return tmp;
			}

			constexpr BasicIterator& operator--() noexcept {
				if (m_ptr && m_ptr->prev != m_owner->capacity()) {
					m_ptr = &m_owner->m_nodes[m_ptr->prev];
				}
				else if (!m_ptr && m_owner->m_tail != m_owner->capacity()) {
					// from end() back to tail
					m_ptr = &m_owner->m_nodes[m_owner->m_tail];
				}
				else {
					m_ptr = nullptr;
				}
				return *this;
			}
			constexpr BasicIterator operator--(int) noexcept {
				BasicIterator tmp = *this;
				--(*this);
				return tmp;
			}

			friend constexpr std::strong_ordering operator<=>(BasicIterator const& a, BasicIterator const& b) noexcept = default;
		};

		using iterator = BasicIterator<false>;
		using const_iterator = BasicIterator<true>;

	private:
		constexpr std::size_t GetIndex(const_iterator it) const noexcept {
			if (it.m_ptr == nullptr) return Capacity;
			return static_cast<std::size_t>(it.m_ptr - m_nodes.data());
		}

	public:
		// constructors
		constexpr StaticList() noexcept {
			InitFreeList();
		}

		// capacity
		constexpr std::size_t size() const noexcept { return m_size; }
		constexpr bool empty() const noexcept { return m_size == 0; }
		static constexpr std::size_t capacity() noexcept { return Capacity; }

		// iterators
		constexpr iterator end() noexcept {
			return iterator(nullptr, this);
		}
		constexpr const_iterator end() const noexcept {
			return const_iterator(nullptr, this);
		}
		constexpr const_iterator cend() const noexcept { return end(); }

		constexpr iterator begin() noexcept {
			if (m_head == Capacity) {
				return end();
			}
			return iterator(&m_nodes[m_head], this);
		}
		constexpr const_iterator begin() const noexcept {
			if (m_head == Capacity) {
				return end();
			}
			return const_iterator(&m_nodes[m_head], this);
		}
		constexpr const_iterator cbegin() const noexcept { return begin(); }

		// element access
		constexpr reference front() noexcept { return *m_nodes[m_head].value; }
		constexpr const_reference front() const noexcept { return *m_nodes[m_head].value; }
		constexpr reference back() noexcept { return *m_nodes[m_tail].value; }
		constexpr const_reference back() const noexcept { return *m_nodes[m_tail].value; }

		// push / pop
		constexpr bool push_front(T const& value) {
			return push_front(T(value));
		}
		constexpr bool push_front(T&& value) {
			std::size_t idx = InsertAfter(Capacity, std::move(value));
			return idx != Capacity;
		}
		constexpr bool push_back(T const& value) {
			return push_back(T(value));
		}
		constexpr bool push_back(T&& value) {
			std::size_t idx = InsertAfter(m_tail, std::move(value));
			return idx != Capacity;
		}
		constexpr void pop_front() noexcept {
			if (m_head != Capacity) EraseNode(m_head);
		}
		constexpr void pop_back() noexcept {
			if (m_tail != Capacity) EraseNode(m_tail);
		}

		// emplace
		template <class... Args>
		constexpr bool emplace_front(Args&&... args) {
			if (!empty()) return false;
			std::size_t idx = InsertAfter(Capacity, std::forward<Args>(args)...);
			return idx != Capacity;
		}
		template <class... Args>
		constexpr bool emplace_back(Args&&... args) {
			if (!empty()) return false;
			std::size_t idx = InsertAfter(m_tail, std::forward<Args>(args)...);
			return idx != Capacity;
		}
		template <class... Args>
		constexpr iterator emplace(const_iterator pos, Args&&... args) {
			if (!empty()) return end();
			if (pos == end()) {
				std::size_t idx = InsertAfter(m_tail, std::forward<Args>(args)...);
				return idx == Capacity ? end() : iterator(&m_nodes[idx], this);
			}
			if (pos == begin()) {
				std::size_t idx = InsertAfter(Capacity, std::forward<Args>(args)...);
				return idx == Capacity ? end() : iterator(&m_nodes[idx], this);
			}
			// get index of the node before pos
			Node const* pos_node = pos.m_ptr;
			std::size_t pos_idx = static_cast<std::size_t>(pos_node - m_nodes.data());
			std::size_t prev_idx = m_nodes[pos_idx].prev;
			std::size_t new_idx = InsertAfter(prev_idx, std::forward<Args>(args)...);
			return new_idx == Capacity ? end() : iterator(&m_nodes[new_idx], this);
		}

		// insert (lvalue/rvalue forwarding to emplace)
		constexpr iterator insert(const_iterator pos, T const& value) {
			return emplace(pos, value);
		}
		constexpr iterator insert(const_iterator pos, T&& value) {
			return emplace(pos, std::move(value));
		}

		// erase
		constexpr iterator erase(const_iterator pos) noexcept {
			if (pos == end()) return end();
			Node const* pos_node = pos.m_ptr;
			std::size_t idx = static_cast<std::size_t>(pos_node - m_nodes.data());
			std::size_t next = m_nodes[idx].next;
			EraseNode(idx);
			return next == Capacity ? end() : iterator(&m_nodes[next], this);
		}

		// clear
		constexpr void clear() noexcept {
			for (std::size_t i = 0; i < Capacity; ++i) {
				m_nodes[i].value.reset();
			}
			InitFreeList();
			m_head = Capacity;
			m_tail = Capacity;
			m_size = 0;
		}

		constexpr void splice(const_iterator pos, StaticList& other) noexcept {
			if (this == &other) return;
			if (other.empty()) return;
			std::size_t prev_idx = (pos == end()) ? m_tail : GetIndex(pos);
			LinkChainAfter(other.m_head, other.m_tail, other.m_size, prev_idx);
			other.m_head = other.m_tail = Capacity;
			other.m_size = 0;
		}

		constexpr void splice(const_iterator pos, StaticList& other, const_iterator it) noexcept {
			if (it == other.end()) return;
			std::size_t src_idx = GetIndex(it);
			if (this == &other && pos == it) return;
			std::size_t prev_idx = (pos == end()) ? m_tail : GetIndex(pos);
			other.RemoveNode(src_idx);
			--other.m_size;
			InsertNodeAfter(src_idx, prev_idx);
		}

		constexpr void splice(const_iterator pos, StaticList& other,
			const_iterator first, const_iterator last) noexcept {
			if (first == last) return;
			std::size_t prev_idx = (pos == end()) ? m_tail : GetIndex(pos);
			std::size_t indices[Capacity]{};
			std::size_t count = 0;
			for (auto it = first; it != last; ++it) {
				indices[count++] = GetIndex(it);
			}
			std::size_t sub_head = Capacity, sub_tail = Capacity;
			for (std::size_t i = 0; i < count; ++i) {
				std::size_t idx = indices[i];
				other.RemoveNode(idx);
				--other.m_size;
				if (sub_head == Capacity) {
					sub_head = sub_tail = idx;
					m_nodes[idx].prev = Capacity;
					m_nodes[idx].next = Capacity;
				}
				else {
					m_nodes[sub_tail].next = idx;
					m_nodes[idx].prev = sub_tail;
					m_nodes[idx].next = Capacity;
					sub_tail = idx;
				}
			}
			LinkChainAfter(sub_head, sub_tail, count, prev_idx);
		}
	};

} // namespace plastic::ds
