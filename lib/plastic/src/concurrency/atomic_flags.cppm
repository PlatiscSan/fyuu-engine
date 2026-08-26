module;
#include <version>
#include <cassert>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <span>
#endif // !defined(__cpp_lib_modules)
export module plastic.atomic_flags;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace {
	template <class Enum>
	constexpr std::size_t EnumSize = []() -> std::size_t {
		static_assert(std::is_enum_v<Enum>, "Not an enumeration type");
		static_assert(requires { Enum::Count; },
			"Enum must have a 'Count' enumerator representing the number of values");
		return static_cast<std::size_t>(Enum::Count);
		}();
}

namespace plastic::concurrency {

	export template <class Enum, class Word = std::size_t> class AtomicFlags {	
	private:
		static_assert(std::is_unsigned_v<Word>, "Word must be an unsigned integral type");
		static_assert(EnumSize<Enum> > 0, "Enum must have at least one value");

		static constexpr std::size_t kBitsPerWord = sizeof(Word) * 8;
		static constexpr std::size_t kWordCount = (EnumSize<Enum> +kBitsPerWord - 1) / kBitsPerWord;

		struct Location { std::size_t idx; Word mask; };

		std::array<std::atomic<Word>, kWordCount> m_bits;

		static constexpr Word LowMask(std::size_t n) noexcept {
			if (n >= kBitsPerWord) {
				return ~static_cast<Word>(0);
			}
			return (static_cast<Word>(1) << n) - 1;
		}

		static constexpr Word kLastWordMask = 
			(EnumSize<Enum> % kBitsPerWord == 0) ? 
			~static_cast<Word>(0) : 
			LowMask(EnumSize<Enum> % kBitsPerWord);

		static constexpr Location Locate(Enum e) noexcept {
			std::size_t idx = static_cast<std::size_t>(e);
			std::size_t word_idx = idx / kBitsPerWord;
			Word bit_mask = static_cast<Word>(1) << (idx % kBitsPerWord);
			return { word_idx, bit_mask };
		}

		static bool AnyInRange(std::array<Word, kWordCount> const& words, Enum from, Enum to) noexcept {
			std::size_t from_idx = static_cast<std::size_t>(from);
			std::size_t to_idx = static_cast<std::size_t>(to);
			if (from_idx > to_idx || to_idx >= EnumSize<Enum>) {
				assert(false && "AnyInRange(): invalid range");
				return false;
			}

			constexpr std::size_t bits = kBitsPerWord;
			std::size_t start_word = from_idx / bits;
			std::size_t start_bit = from_idx % bits;
			std::size_t end_word = to_idx / bits;
			std::size_t end_bit = to_idx % bits;

			if (start_word == end_word) {
				std::size_t len = to_idx - from_idx + 1;
				Word mask = ((static_cast<Word>(1) << len) - 1) << start_bit;
				return (words[start_word] & mask) != 0;
			}
			else {
				Word first_mask = ~((static_cast<Word>(1) << start_bit) - 1);
				if ((words[start_word] & first_mask) != 0)
					return true;
				for (std::size_t w = start_word + 1; w < end_word; ++w) {
					if (words[w] != 0)
						return true;
				}
				Word last_mask = LowMask(end_bit + 1);
				return (words[end_word] & last_mask) != 0;
			}
		}

	public:
		struct Range {
			Enum from;
			Enum to;
		};

		AtomicFlags() noexcept 
			: m_bits() {
			for (auto& w : m_bits) {
				w.store(0, std::memory_order::relaxed);
			}
		}

		AtomicFlags(AtomicFlags const& other) noexcept 
			: AtomicFlags() {
			for (std::size_t i = 0; i < kWordCount; ++i) {
				m_bits[i].store(other.m_bits[i].load(std::memory_order::acquire), std::memory_order::relaxed);
			}
		}

		AtomicFlags& operator=(AtomicFlags const& other) noexcept {
			if (this != &other) {
				for (std::size_t i = 0; i < kWordCount; ++i) {
					m_bits[i].store(other.m_bits[i].load(std::memory_order::acquire), std::memory_order::relaxed);
				}
			}
			return *this;
		}

		AtomicFlags(AtomicFlags&& other) noexcept 
			: AtomicFlags() {
			for (std::size_t i = 0; i < kWordCount; ++i) {
				Word val = other.m_bits[i].exchange(0, std::memory_order::release);
				m_bits[i].store(val, std::memory_order::relaxed);
			}
		}

		AtomicFlags& operator=(AtomicFlags&& other) noexcept {
			if (this != &other) {
				for (std::size_t i = 0; i < kWordCount; ++i) {
					Word val = other.m_bits[i].exchange(0, std::memory_order::release);
					m_bits[i].store(val, std::memory_order::relaxed);
				}
			}
			return *this;
		}

		bool TestAndSet(Enum e, bool value = true, std::memory_order success = std::memory_order::acq_rel, std::memory_order failure = std::memory_order::relaxed) noexcept {
			auto [idx, mask] = Locate(e);
			Word old = m_bits[idx].load(std::memory_order::relaxed);
			bool exchanged = false;
			do {
				bool was_set = (old & mask) != 0;
				Word desired = value ? (old | mask) : (old & ~mask);
				if (desired == old) {
					return was_set;
				}
				exchanged = m_bits[idx].compare_exchange_weak(old, desired, success, failure);
				if (exchanged) {
					return was_set;
				}
			} while (!exchanged);
			return false;
		}

		bool Test(Enum e, std::memory_order mem_order = std::memory_order::acquire) const noexcept {
			auto [idx, mask] = Locate(e);
			return (m_bits[idx].load(mem_order) & mask) != 0;
		}

		bool Any(std::memory_order mem_order = std::memory_order::acquire) const noexcept {
			for (std::size_t i = 0; i < kWordCount; ++i) {
				if (m_bits[i].load(mem_order) != 0) {
					return true;
				}
			}
			return false;
		}

		bool None(std::memory_order mem_order = std::memory_order::acquire) const noexcept {
			return !Any(mem_order);
		}

		std::size_t Count(std::memory_order mem_order = std::memory_order::acquire) const noexcept {
			std::size_t total = 0;
			for (std::size_t i = 0; i < kWordCount; ++i) {
				total += std::popcount(m_bits[i].load(mem_order));
			}
			return total;
		}

		void Set(Enum e, bool value = true, std::memory_order mem_order = std::memory_order::release) noexcept {
			auto [idx, mask] = Locate(e);
			if (value) {
				m_bits[idx].fetch_or(mask, mem_order);
			}
			else {
				m_bits[idx].fetch_and(~mask, mem_order);
			}
		}

		explicit AtomicFlags(Enum e) noexcept
			: AtomicFlags() {
			Set(e);
		}

		template <class... Enums>
			requires (sizeof...(Enums) >= 2) && (std::same_as<Enum, Enums> && ...)
		AtomicFlags(Enums&&... enums) noexcept
			: AtomicFlags() {
			(Set(std::forward<Enums>(enums)), ...);
		}

		void Reset(Enum e, std::memory_order mem_order = std::memory_order::release) noexcept {
			Set(e, false, mem_order);
		}

		void Flip(Enum e, std::memory_order mem_order = std::memory_order::release) noexcept {
			auto [idx, mask] = Locate(e);
			m_bits[idx].fetch_xor(mask, mem_order);
		}

		void SetAll(std::memory_order mem_order = std::memory_order::release) noexcept {
			for (std::size_t i = 0; i < kWordCount - 1; ++i) {
				m_bits[i].store(~static_cast<Word>(0), mem_order);
			}
			m_bits[kWordCount - 1].store(kLastWordMask, mem_order);
		}

		void ResetAll(std::memory_order mem_order = std::memory_order::release) noexcept {
			for (auto& w : m_bits) {
				w.store(0, mem_order);
			}
		}

		void FlipAll(std::memory_order mem_order = std::memory_order::release) noexcept {
			for (std::size_t i = 0; i < kWordCount - 1; ++i) {
				m_bits[i].fetch_xor(~static_cast<Word>(0), mem_order);
			}
			m_bits[kWordCount - 1].fetch_xor(kLastWordMask, mem_order);
		}

		std::array<Word, kWordCount> Snapshot(std::memory_order mem_order = std::memory_order::relaxed) const noexcept {
			std::array<Word, kWordCount> result{};
			for (std::size_t i = 0; i < kWordCount; ++i) {
				result[i] = m_bits[i].load(mem_order);
			}
			return result;
		}

		std::array<Word, kWordCount> ConsistentSnapshot() const noexcept {
			if constexpr (kWordCount == 1) {
				return { m_bits[0].load(std::memory_order_acquire) };
			}
			else {
				std::array<Word, kWordCount> old, cur;
				for (std::size_t i = 0; i < kWordCount; ++i)
					old[i] = m_bits[i].load(std::memory_order_acquire);
				bool stable = false;
				while (!stable) {
					for (std::size_t i = 0; i < kWordCount; ++i)
						cur[i] = m_bits[i].load(std::memory_order_acquire);
					stable = cur == old;
					if (!stable)
						old = cur;
				}
				return cur;
			}
		}

		void Assign(std::array<Word, kWordCount> const& words, std::memory_order mem_order = std::memory_order::release) noexcept {
			for (std::size_t i = 0; i < kWordCount; ++i) {
				m_bits[i].store(words[i], mem_order);
			}
		}

		AtomicFlags& operator|=(AtomicFlags const& other) noexcept {
			for (std::size_t i = 0; i < kWordCount; ++i) {
				Word val = other.m_bits[i].load(std::memory_order::acquire);
				m_bits[i].fetch_or(val, std::memory_order::release);
			}
			return *this;
		}

		AtomicFlags& operator&=(AtomicFlags const& other) noexcept {
			for (std::size_t i = 0; i < kWordCount; ++i) {
				Word val = other.m_bits[i].load(std::memory_order::acquire);
				m_bits[i].fetch_and(val, std::memory_order::release);
			}
			return *this;
		}

		AtomicFlags& operator^=(AtomicFlags const& other) noexcept {
			for (std::size_t i = 0; i < kWordCount; ++i) {
				Word val = other.m_bits[i].load(std::memory_order::acquire);
				m_bits[i].fetch_xor(val, std::memory_order::release);
			}
			return *this;
		}

		AtomicFlags& operator|=(Enum e) noexcept {
			auto [idx, mask] = Locate(e);
			m_bits[idx].fetch_or(mask, std::memory_order::release);
			return *this;
		}

		AtomicFlags& operator&=(Enum e) noexcept {
			auto [idx, mask] = Locate(e);
			m_bits[idx].fetch_and(mask, std::memory_order::release);
			return *this;
		}

		AtomicFlags& operator^=(Enum e) noexcept {
			auto [idx, mask] = Locate(e);
			m_bits[idx].fetch_xor(mask, std::memory_order::release);
			return *this;
		}
	
		bool TestContinuous(Enum from, Enum to, std::memory_order mem_order = std::memory_order::acquire) const noexcept {
			std::size_t from_idx = static_cast<std::size_t>(from);
			std::size_t to_idx = static_cast<std::size_t>(to);
			if (from_idx > to_idx || to_idx >= EnumSize<Enum>) {
				assert(false && "TestContinuous(): invalid range");
				return false;
			}
			std::size_t length = to_idx - from_idx + 1;

			Word local_words[kWordCount]{};
			for (std::size_t i = 0; i < kWordCount; ++i) {
				local_words[i] = m_bits[i].load(mem_order);
			}

			std::size_t start_word = from_idx / kBitsPerWord;
			std::size_t start_bit = from_idx % kBitsPerWord;
			std::size_t end_word = to_idx / kBitsPerWord;
			std::size_t end_bit = to_idx % kBitsPerWord;

			if (start_word == end_word) {
				Word mask = LowMask(length) << start_bit;
				return (local_words[start_word] & mask) == mask;
			}
			else {
				Word first_mask = ~((static_cast<Word>(1) << start_bit) - 1);
				if ((local_words[start_word] & first_mask) != first_mask) {
					return false;
				}
				for (std::size_t w = start_word + 1; w < end_word; ++w) {
					if (local_words[w] != ~static_cast<Word>(0)) {
						return false;
					}
				}
				Word last_mask = (static_cast<Word>(1) << (end_bit + 1)) - 1;
				return (local_words[end_word] & last_mask) == last_mask;
			}
		}

		bool TestAnyInRange(Enum from, Enum to, std::memory_order mem_order = std::memory_order::relaxed) const noexcept {
			auto words = Snapshot(mem_order);
			return AtomicFlags::AnyInRange(words, from, to);
		}

		bool TestSingleInRange(Enum from, Enum to, std::memory_order mem_order = std::memory_order::acquire) const noexcept {
		
			std::size_t from_idx = static_cast<std::size_t>(from);
			std::size_t to_idx = static_cast<std::size_t>(to);
	
			if (from_idx > to_idx || to_idx >= EnumSize<Enum>) {
				assert(false && "TestSingleInRange(): invalid range");
				return false;
			}
	
			Word local_words[kWordCount];
			for (std::size_t i = 0; i < kWordCount; ++i) {
				local_words[i] = m_bits[i].load(mem_order);
			}
	
			std::size_t start_word = from_idx / kBitsPerWord;
			std::size_t start_bit= from_idx % kBitsPerWord;
			std::size_t end_word = to_idx / kBitsPerWord;
			std::size_t end_bit= to_idx % kBitsPerWord;
			std::size_t length = to_idx - from_idx + 1;
	
			if (start_word == end_word) {
				Word mask = LowMask(length) << start_bit;
				Word masked = local_words[start_word] & mask;
				return std::popcount(masked) == 1;
			} 
			else {
				int count = 0;

				Word first_mask = ~((static_cast<Word>(1) << start_bit) - 1);
				count += std::popcount(local_words[start_word] & first_mask);
				if (count > 1) {
					return false;
				}
		
				for (std::size_t w = start_word + 1; w < end_word; ++w) {
					count += std::popcount(local_words[w]);
					if (count > 1) {
						return false;
					}
				}
	
				Word last_mask = (static_cast<Word>(1) << (end_bit + 1)) - 1;
				count += std::popcount(local_words[end_word] & last_mask);
		
				return count == 1;
			}
		}

		bool TestMultipleInRange(
			Enum from,
			Enum to,
			std::memory_order mem_order = std::memory_order::acquire
		) const noexcept {
			std::size_t from_idx = static_cast<std::size_t>(from);
			std::size_t to_idx = static_cast<std::size_t>(to);
			if (from_idx > to_idx || to_idx >= EnumSize<Enum>) {
				assert(false && "TestMultipleInRange(): invalid range");
				return false;
			}
			auto words = Snapshot(mem_order);
			std::size_t count = 0u;
			for (std::size_t index = from_idx; index <= to_idx; ++index) {
				auto word = index / kBitsPerWord;
				auto mask = static_cast<Word>(1) << (index % kBitsPerWord);
				if ((words[word] & mask) != 0u && ++count > 1u) {
					return true;
				}
			}
			return false;
		}

		bool CheckMutualExclusion(std::span<Range const> groups, std::memory_order mem_order = std::memory_order::relaxed) const {
			auto words = Snapshot(mem_order);
			std::size_t active = 0;
			for (auto& g : groups) {
				if (AtomicFlags::AnyInRange(words, g.from, g.to)) {
					if (++active > 1) return false;
				}
			}
			return true;
		}

	};

	template <class Enum, class Word>
	AtomicFlags<Enum, Word> operator|(AtomicFlags<Enum, Word> const& a, AtomicFlags<Enum, Word> const& b) {
		AtomicFlags<Enum, Word> result;
		auto a_words = a.Snapshot();
		auto b_words = b.Snapshot();
		for (std::size_t i = 0; i < a_words.size(); ++i) {
			a_words[i] |= b_words[i];
		}
		result.Assign(a_words);
		return result;
	}

	template <class Enum, class Word>
	AtomicFlags<Enum, Word> operator&(AtomicFlags<Enum, Word> const& a, AtomicFlags<Enum, Word> const& b) {
		AtomicFlags<Enum, Word> result;
		auto a_words = a.Snapshot();
		auto b_words = b.Snapshot();
		for (std::size_t i = 0; i < a_words.size(); ++i) {
			a_words[i] &= b_words[i];
		}
		result.Assign(a_words);
		return result;
	}

	template <class Enum, class Word>
	AtomicFlags<Enum, Word> operator^(AtomicFlags<Enum, Word> const& a, AtomicFlags<Enum, Word> const& b) {
		AtomicFlags<Enum, Word> result;
		auto a_words = a.Snapshot();
		auto b_words = b.Snapshot();
		for (std::size_t i = 0; i < a_words.size(); ++i) {
			a_words[i] ^= b_words[i];
		}
		result.Assign(a_words);
		return result;
	}

	template <class Enum>
	AtomicFlags<Enum> operator|(Enum lhs, Enum rhs) {
		AtomicFlags<Enum> result;
		result.Set(lhs);
		result.Set(rhs);
		return result;
	}


} // namespace plastic::concurrency
