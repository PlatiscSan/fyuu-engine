module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>

#include <string>

#include <array>

#include <string_view>
#endif // !defined(__cpp_lib_modules)

export module fyuu_asset:uuid;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_asset {

	/**
	 * @brief Stable asset uuid stored as 16 network-order bytes.
	 *
	 * An all-zero value is the nil uuid and is never persisted by the asset
	 * catalog. The representation owns no memory, so copying an uuid never
	 * allocates and comparison is one 16-byte value comparison.
	 */
	using UUID = std::array<std::byte, 16u>;

	/**
	 * @brief Parses the text form of one asset uuid.
	 *
	 * Accepts the hyphenated form in either letter case, the hyphen-free form and
	 * the braced form written by catalog files.
	 *
	 * @param text Text form of one asset uuid.
	 * @return Parsed uuid bytes.
	 * @throws std::invalid_argument if @p text is not a well-formed UUID.
	 */
	[[nodiscard]] UUID ParseUUID(std::string_view text);

	/**
	 * @brief Formats one asset uuid as lowercase hyphenated text.
	 * @param uuid Asset uuid bytes.
	 * @return Canonical 36-character text form.
	 */
	[[nodiscard]] std::string FormatUUID(UUID const& uuid);

	/**
	 * @brief Returns the nil uuid with all bytes set to zero.
	 * @return Nil uuid.
	 */
	[[nodiscard]]
	constexpr UUID NilUUID() noexcept {
		return UUID{};
	}

} // namespace fyuu_asset
