module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <memory>
#include <new>
#include <utility>

#include <type_traits>
#endif // !defined(__cpp_lib_modules)

export module fyuu_rhi:fast_pimpl;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_rhi::details {

	/** @brief Capacity shared by completion-token storage and its queued receiver capture. */
	inline constexpr std::size_t CompletionTokenStorageSize = 320u;

	/**
	 * @brief Owns an optional implementation in fixed inline storage without allocating.
	 * @note Owners define their special members where Implementation is complete.
	 * Implementation must move without throwing, including native handles whose
	 * generated declarations omit noexcept. Moving leaves the source empty; borrowed
	 * implementation addresses remain valid only until the owner moves or dies.
	 */
	template <class Implementation, std::size_t Size, std::size_t Alignment = alignof(void*)>
	class FastPimpl {
	private:
		alignas(Alignment) mutable std::byte m_storage[Size];
		bool m_engaged = false;

		/** @brief Ends the lifetime of an engaged inline implementation. */
		void Reset() noexcept {
			if (m_engaged) {
				std::destroy_at(get());
				m_engaged = false;
			}
		}

	public:
		FastPimpl() noexcept = default;

		/** @brief Constructs the implementation directly in its checked inline buffer. */
		template <class... Arguments>
		explicit FastPimpl(std::in_place_t, Arguments&&... arguments)
			: m_engaged([&] {
				std::construct_at(reinterpret_cast<Implementation*>(m_storage), std::forward<Arguments>(arguments)...);
				return true;
			}()) {
			static_assert(sizeof(Implementation) <= Size, "FastPimpl buffer is too small");
			static_assert(alignof(Implementation) <= Alignment, "FastPimpl alignment is insufficient");
			static_assert(std::is_nothrow_destructible_v<Implementation>);
		}

		FastPimpl(FastPimpl const&) = delete;
		FastPimpl& operator=(FastPimpl const&) = delete;

		/** @brief Relocates a live implementation and destroys its moved-from source. */
		FastPimpl(FastPimpl&& other) noexcept
			: m_engaged([&] {
				if (!other.m_engaged) {
					return false;
				}
				std::construct_at(reinterpret_cast<Implementation*>(m_storage), std::move(*other.get()));
				other.Reset();
				return true;
			}()) {
		}

		/** @brief Releases the destination before relocating; self-move preserves ownership. */
		FastPimpl& operator=(FastPimpl&& other) noexcept {
			if (this != &other) {
				Reset();
				if (other.m_engaged) {
					std::construct_at(reinterpret_cast<Implementation*>(m_storage), std::move(*other.get()));
					m_engaged = true;
					other.Reset();
				}
			}
			return *this;
		}

		~FastPimpl() noexcept {
			Reset();
		}

		/** @brief Returns whether the buffer contains a live implementation. */
		explicit operator bool() const noexcept {
			return m_engaged;
		}

		/** @brief Borrows the implementation with the same shallow constness as an owning pointer. */
		Implementation* get() const noexcept {
			return m_engaged ? std::launder(reinterpret_cast<Implementation*>(m_storage)) : nullptr;
		}

		/** @brief Accesses an engaged implementation. */
		Implementation* operator->() const noexcept {
			return get();
		}
	};

} // namespace fyuu_rhi::details
