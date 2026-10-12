module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>

#include <vector>

#include <string>

#include <string_view>

#include <filesystem>
#endif // !defined(__cpp_lib_modules)

export module fyuu_asset:import_file_system;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_asset {

	/**
	 * @brief Provides import-relative reads from a directory or ZIP archive.
	 *
	 * The filesystem is scoped to one import operation. Archive entries are
	 * decompressed only when requested and no extracted files are written to disk.
	 * @note Every path accepted or returned by Exists(), Read(), and ListFiles() is a
	 * normalized, import-root-relative UTF-8 virtual path using '/' as its separator.
	 * Virtual paths never expose the host directory or archive location.
	 */
	class ImportFileSystem final {
	private:
		std::filesystem::path m_source; ///< Canonical file, directory root or archive path.
		std::string m_entry;             ///< Single-file entry, or empty for package sources.
		bool m_is_archive;               ///< Selects ZIP-backed reads when true.

		/** @brief Resolves and confines a path to the directory import root. */
		[[nodiscard]] std::filesystem::path ResolveDirectoryPath(std::string_view path) const;

	public:
		/**
		 * @brief Creates an import filesystem from a file, directory or ZIP archive.
		 * @param source Single input file, directory root or ZIP archive path.
		 * @throws std::invalid_argument if @p source does not identify a supported source.
		 */
		explicit ImportFileSystem(std::filesystem::path const& source);

		/**
		 * @brief Reports whether an import-relative regular file exists.
		 * @param path Import-relative virtual path.
		 * @return True when the selected backend contains the file.
		 */
		[[nodiscard]] bool Exists(std::string_view path) const;

		/**
		 * @brief Reads one import-relative file into owned memory.
		 * @param path Import-relative virtual path.
		 * @return Complete file contents.
		 * @throws std::runtime_error if the file cannot be found or read completely.
		 */
		[[nodiscard]] std::vector<std::byte> Read(std::string_view path) const;

		/**
		 * @brief Enumerates every regular file visible through the import filesystem.
		 * @return Import-relative paths accepted by Exists() and Read().
		 * @note Returned virtual paths always use validated UTF-8 and forward slashes.
		 */
		[[nodiscard]] std::vector<std::string> ListFiles() const;
	};

} // namespace fyuu_asset
