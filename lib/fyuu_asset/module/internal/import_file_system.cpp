module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>

#include <string_view>

#include <filesystem>
#endif // !defined(__cpp_lib_modules)

module fyuu_asset:import_file_system_impl;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace fyuu_asset {

	/** @brief Converts a native path into the UTF-8 representation used by virtual paths. */
	[[nodiscard]] std::string ToVirtualPath(std::filesystem::path const& path);

	/** @brief Converts a validated UTF-8 virtual path into a native path value. */
	[[nodiscard]] std::filesystem::path FromVirtualPath(std::string_view path);

	/** @brief Converts legacy CP932 metadata to UTF-8 while preserving existing UTF-8. */
	[[nodiscard]] std::string DecodeLegacyVirtualPath(std::string_view path);

} // namespace fyuu_asset
