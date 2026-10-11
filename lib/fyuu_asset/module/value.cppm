module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <utility>

#include <map>
#include <vector>

#include <functional>

#include <string>

#include <cstdint>

#include <variant>
#endif // !defined(__cpp_lib_modules)

export module fyuu_asset:value;
import :uuid;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_asset {
	class Value;

	using Binary = std::vector<std::byte>; ///< Owned uninterpreted byte sequence.

	/** @brief Owned recognized file persisted beside its containing asset JSON. */
	struct File final {
		std::string m_name; ///< Logical filename including its recognized extension.
		Binary m_data;      ///< Complete file contents.
	};

	using Array = std::vector<Value>;      ///< Owned ordered sequence of dynamic values.
	using Object = std::map<std::string, Value, std::less<>>; ///< Owned named value fields.

	/**
	 * @brief Owned recursive value used by dynamic asset types.
	 *
	 * std::monostate represents null and UUID represents an asset reference
	 * without retaining a loaded asset. A missing object field remains distinct
	 * from a field whose value is null.
	 */
	class Value final {
	private:
		std::variant<
		    std::monostate,
		    bool,
		    std::int64_t,
		    std::uint64_t,
		    double,
		    std::string,
		    Binary,
		    File,
		    Array,
		    Object,
		    UUID>
		    m_data; ///< Active dynamic value alternative.

	public:
		/** @brief Constructs a null dynamic value. */
		Value() noexcept;

		/** @brief Copies a dynamic value. */
		Value(Value const& other);

		/** @brief Moves a dynamic value. */
		Value(Value&& other) noexcept;

		/** @brief Copies another dynamic value into this value. */
		Value& operator=(Value const& other);

		/** @brief Moves another dynamic value into this value. */
		Value& operator=(Value&& other) noexcept;

		/** @brief Destroys the active dynamic value outside importing translation units. */
		~Value();

		/** @brief Constructs a boolean dynamic value. */
		Value(bool value) noexcept;

		/** @brief Constructs a signed integer dynamic value. */
		Value(std::int64_t value) noexcept;

		/** @brief Constructs an unsigned integer dynamic value. */
		Value(std::uint64_t value) noexcept;

		/** @brief Constructs a floating-point dynamic value. */
		Value(double value) noexcept;

		/** @brief Copies a string into a dynamic value. */
		Value(std::string const& value);

		/** @brief Moves a string into a dynamic value. */
		Value(std::string&& value) noexcept;

		/** @brief Copies binary data into a dynamic value. */
		Value(Binary const& value);

		/** @brief Moves binary data into a dynamic value. */
		Value(Binary&& value) noexcept;

		/** @brief Copies a recognized attached file into a dynamic value. */
		Value(File const& value);

		/** @brief Moves a recognized attached file into a dynamic value. */
		Value(File&& value) noexcept;

		/** @brief Copies an array into a dynamic value. */
		Value(Array const& value);

		/** @brief Moves an array into a dynamic value. */
		Value(Array&& value) noexcept;

		/** @brief Copies an object into a dynamic value. */
		Value(Object const& value);

		/** @brief Moves an object into a dynamic value. */
		Value(Object&& value) noexcept;

		/** @brief Copies an asset identity into a dynamic value. */
		Value(UUID const& value) noexcept;

		/**
		 * @brief Accesses the active value alternative as @p T.
		 * @tparam T Dynamic value alternative to access.
		 * @return Active value with constness and value category matching this object.
		 * @throws std::bad_variant_access if @p T is not the active alternative.
		 */
		template <class T>
		[[nodiscard]] decltype(auto) As(this auto&& self) {
			return std::get<T>(std::forward_like<decltype(self)>(self.m_data));
		}

		/**
		 * @brief Reports whether @p T is the active value alternative.
		 * @tparam T Dynamic value alternative to test.
		 * @return True when @p T is active.
		 */
		template <class T>
		[[nodiscard]] bool Is(this Value const& self) noexcept {
			return std::holds_alternative<T>(self.m_data);
		}
	};

} // namespace fyuu_asset
