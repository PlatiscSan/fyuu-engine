// ============================================================================
// other.cppm - Module interface for miscellaneous utilities
// ============================================================================
//
// This module provides two small but useful utilities:
//   - Defer<Func> : a RAII class that executes a function upon destruction
//                   (similar to Go's `defer` statement).

module;
#include <version>
#if !defined(__cpp_lib_modules)
#endif // !defined(__cpp_lib_modules)
export module plastic.other;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace plastic::utility {

	// ------------------------------------------------------------------------
	// Defer – execute a function on scope exit
	// ------------------------------------------------------------------------

	/**
	 * @brief A RAII wrapper that invokes a supplied function when the Defer
	 *        object goes out of scope. It is move‑only and cannot be copied.
	 *
	 * Typical use:
	 * @code
	 *   auto d = Defer([]{ cleanup(); });
	 * @endcode
	 *
	 * @tparam Func The type of the function object to invoke. It must be
	 *              callable without arguments and should not throw exceptions
	 *              (though exceptions are caught and ignored in the destructor).
	 */
	export template <class Func>
	class Defer final {
	private:
		Func m_func;   ///< Stored function object.

	public:
		/**
		 * @brief Constructs a Defer from a function object.
		 * @param func The function to be called on destruction.
		 */
		Defer(Func&& func) : m_func(std::move(func)) {}

		// Copy is disallowed – a deferred action should not be duplicated.
		Defer(Defer const&) = delete;
		Defer& operator=(Defer const&) = delete;

		// Move is allowed (transfers ownership of the function).
		Defer(Defer&&) = default;
		Defer& operator=(Defer&&) = default;

		/// Destructor invokes the stored function. Any exception is swallowed.
		~Defer() noexcept {
			try {
				std::invoke(m_func);
			} catch (...) {
				// According to the design, the function should not throw,
				// but if it does, we ignore it to satisfy noexcept.
			}
		}
	};

} // namespace plastic::utility