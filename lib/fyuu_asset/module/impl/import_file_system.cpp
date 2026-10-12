module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <utility>

#include <vector>

#include <algorithm>

#include <string>

#include <limits>

#include <fstream>

#include <string_view>

#include <filesystem>
#endif // !defined(__cpp_lib_modules)
#include <boost/locale/encoding.hpp>
#include <minizip/unzip.h>

module fyuu_asset;
import :import_file_system;
import :import_file_system_impl;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace fyuu_asset {

	namespace {
#if defined(_WIN32)
		/** @brief Opens a physical archive through its native UTF-16 Windows path. */
		voidpf OpenArchiveFile(voidpf, void const* filename, int mode) {
			if (!filename || (mode & ZLIB_FILEFUNC_MODE_READ) == 0) {
				return nullptr;
			}
			FILE* file = nullptr;
			if (_wfopen_s(&file, static_cast<wchar_t const*>(filename), L"rb") != 0) {
				return nullptr;
			}
			return file;
		}

		/** @brief Reads bytes from one Minizip-owned Windows archive stream. */
		uLong ReadArchiveFile(voidpf, voidpf stream, void* output, uLong size) {
			return static_cast<uLong>(::fread(output, 1u, size, static_cast<FILE*>(stream)));
		}

		/** @brief Rejects writes because imported archives are read-only. */
		uLong WriteArchiveFile(voidpf, voidpf, void const*, uLong) {
			return 0u;
		}

		/** @brief Reports the current 64-bit archive stream position. */
		ZPOS64_T TellArchiveFile(voidpf, voidpf stream) {
			auto const position = _ftelli64(static_cast<FILE*>(stream));
			return position < 0 ? static_cast<ZPOS64_T>(-1) : static_cast<ZPOS64_T>(position);
		}

		/** @brief Moves one Windows archive stream using a 64-bit offset. */
		long SeekArchiveFile(voidpf, voidpf stream, ZPOS64_T offset, int origin) {
			if (offset > static_cast<ZPOS64_T>(std::numeric_limits<std::int64_t>::max())) {
				return -1;
			}
			int native_origin = SEEK_SET;
			switch (origin) {
				case ZLIB_FILEFUNC_SEEK_CUR:
					native_origin = SEEK_CUR;
					break;
				case ZLIB_FILEFUNC_SEEK_END:
					native_origin = SEEK_END;
					break;
				case ZLIB_FILEFUNC_SEEK_SET:
					break;
				default:
					return -1;
			}
			return _fseeki64(
			    static_cast<FILE*>(stream),
			    static_cast<std::int64_t>(offset),
			    native_origin
			);
		}

		/** @brief Closes one Windows archive stream. */
		int CloseArchiveFile(voidpf, voidpf stream) {
			return ::fclose(static_cast<FILE*>(stream));
		}

		/** @brief Reports the standard I/O error state of one archive stream. */
		int ArchiveFileError(voidpf, voidpf stream) {
			return ::ferror(static_cast<FILE*>(stream));
		}

		/** @brief Opens Minizip with native Windows paths instead of locale-encoded bytes. */
		[[nodiscard]] unzFile OpenArchive(std::filesystem::path const& path) {
			zlib_filefunc64_def functions{
			    .zopen64_file = OpenArchiveFile,
			    .zread_file = ReadArchiveFile,
			    .zwrite_file = WriteArchiveFile,
			    .ztell64_file = TellArchiveFile,
			    .zseek64_file = SeekArchiveFile,
			    .zclose_file = CloseArchiveFile,
			    .zerror_file = ArchiveFileError,
			    .opaque = nullptr
			};
			return unzOpen2_64(path.c_str(), &functions);
		}
#else
		/** @brief Opens Minizip with the platform-native narrow physical path. */
		[[nodiscard]] unzFile OpenArchive(std::filesystem::path const& path) {
			return unzOpen64(path.c_str());
		}
#endif // defined(_WIN32)

		/** @brief Converts a native filesystem path into forward-slashed UTF-8. */
		[[nodiscard]] std::string PathToUTF8(std::filesystem::path const& path) {
			auto const encoded = path.generic_u8string();
			return {
			    reinterpret_cast<char const*>(encoded.data()),
			    encoded.size()
			};
		}

		/** @brief Converts UTF-8 virtual path text into a native filesystem path. */
		[[nodiscard]] std::filesystem::path PathFromUTF8(std::string_view path) {
			std::u8string encoded;
			encoded.assign(
			    reinterpret_cast<char8_t const*>(path.data()),
			    reinterpret_cast<char8_t const*>(path.data() + path.size())
			);
			return std::filesystem::path{encoded};
		}

		/** @brief Validates one byte string as UTF-8 and returns an owning copy. */
		[[nodiscard]] std::string ValidateUTF8(std::string_view text) {
			try {
				return boost::locale::conv::to_utf<char>(
				    text.data(),
				    text.data() + text.size(),
				    "UTF-8",
				    boost::locale::conv::stop
				);
			} catch (boost::locale::conv::conversion_error const&) {
				throw std::invalid_argument{"Virtual asset path is not valid UTF-8"};
			}
		}

		/** @brief Decodes one ZIP entry name into the UTF-8 virtual path encoding. */
		[[nodiscard]] std::string DecodeArchiveName(
		    std::string_view name,
		    unsigned long flags
		) {
			try {
				return ValidateUTF8(name);
			} catch (std::invalid_argument const&) {
				if ((flags & (1ul << 11u)) != 0u) {
					throw;
				}
			}
			try {
				return boost::locale::conv::to_utf<char>(
				    name.data(),
				    name.data() + name.size(),
				    "CP932",
				    boost::locale::conv::stop
				);
			} catch (boost::locale::conv::conversion_error const&) {
				throw std::invalid_argument{
				    "ZIP entry name is neither UTF-8 nor supported legacy CP932"
				};
			}
		}

		/** @brief Owns one Minizip archive handle for a single filesystem operation. */
		class ArchiveHandle final {
		private:
			unzFile m_handle;

		public:
			explicit ArchiveHandle(std::filesystem::path const& path) :
			    m_handle(OpenArchive(path)) {
				if (!m_handle) {
					throw std::runtime_error{"Failed to open asset archive"};
				}
			}

			ArchiveHandle(ArchiveHandle const&) = delete;
			ArchiveHandle& operator=(ArchiveHandle const&) = delete;

			~ArchiveHandle() {
				unzClose(m_handle);
			}

			[[nodiscard]] unzFile Get() const noexcept {
				return m_handle;
			}
		};

		/** @brief Closes the selected ZIP entry when reading leaves its scope. */
		class ArchiveEntry final {
		private:
			unzFile m_archive;

		public:
			explicit ArchiveEntry(unzFile archive) : m_archive(archive) {
				if (unzOpenCurrentFile(m_archive) != UNZ_OK) {
					throw std::runtime_error{"Failed to open file in asset archive"};
				}
			}

			ArchiveEntry(ArchiveEntry const&) = delete;
			ArchiveEntry& operator=(ArchiveEntry const&) = delete;

			~ArchiveEntry() {
				unzCloseCurrentFile(m_archive);
			}
		};

		/**
		 * @brief Normalizes an import-relative path and rejects root escapes.
		 * @param path Path supplied by an importer or one of its dependencies.
		 * @return Normalized path using forward slashes.
		 */
		[[nodiscard]] std::string NormalizeVirtualPath(std::string_view path) {
			if (path.empty() || path.contains('\0')) {
				throw std::invalid_argument{
				    "Virtual asset path must not be empty or contain null characters"
				};
			}

			std::string text = ValidateUTF8(path);
			std::ranges::replace(text, '\\', '/');
			std::filesystem::path normalized = PathFromUTF8(text).lexically_normal();
			if (normalized.is_absolute() || normalized.has_root_name() ||
			    normalized.has_root_directory()) {
				throw std::invalid_argument{"Virtual asset path must be relative"};
			}
			for (auto const& component : normalized) {
				if (component == "..") {
					throw std::invalid_argument{"Virtual asset path escapes its import root"};
				}
			}

			auto result = PathToUTF8(normalized);
			if (result.empty() || result == ".") {
				throw std::invalid_argument{"Virtual asset path does not name a file"};
			}
			return result;
		}

		/** @brief Reads the raw byte name and metadata of the selected ZIP entry. */
		[[nodiscard]] std::pair<std::string, unz_file_info64> CurrentArchiveName(
		    unzFile archive
		) {
			unz_file_info64 information{};
			if (unzGetCurrentFileInfo64(
			        archive,
			        &information,
			        nullptr,
			        0,
			        nullptr,
			        0,
			        nullptr,
			        0
			    ) != UNZ_OK) {
				throw std::runtime_error{"Failed to inspect file in asset archive"};
			}
			std::string name(information.size_filename + 1u, '\0');
			if (unzGetCurrentFileInfo64(
			        archive,
			        &information,
			        name.data(),
			        static_cast<unsigned long>(name.size()),
			        nullptr,
			        0,
			        nullptr,
			        0
			    ) != UNZ_OK) {
				throw std::runtime_error{"Failed to read a filename from asset archive"};
			}
			name.resize(information.size_filename);
			return {std::move(name), information};
		}

		/** @brief Selects the ZIP entry named by one normalized UTF-8 virtual path. */
		[[nodiscard]] bool LocateArchiveFile(unzFile archive, std::string_view path) {
			auto const expected = NormalizeVirtualPath(path);
			auto status = unzGoToFirstFile(archive);
			while (status == UNZ_OK) {
				auto [raw_name, information] = CurrentArchiveName(archive);
				if (!raw_name.empty() && raw_name.back() != '/' && raw_name.back() != '\\' &&
				    NormalizeVirtualPath(DecodeArchiveName(raw_name, information.flag)) == expected) {
					return true;
				}
				status = unzGoToNextFile(archive);
			}
			if (status != UNZ_END_OF_LIST_OF_FILE) {
				throw std::runtime_error{"Failed to enumerate asset archive"};
			}
			return false;
		}
	} // namespace

	/** @brief Converts a native path into the UTF-8 representation used by virtual paths. */
	[[nodiscard]] std::string ToVirtualPath(std::filesystem::path const& path) {
		return PathToUTF8(path);
	}

	/** @brief Converts a validated UTF-8 virtual path into a native path value. */
	[[nodiscard]] std::filesystem::path FromVirtualPath(std::string_view path) {
		return PathFromUTF8(ValidateUTF8(path));
	}

	/** @brief Converts legacy CP932 metadata to UTF-8 while preserving existing UTF-8. */
	[[nodiscard]] std::string DecodeLegacyVirtualPath(std::string_view path) {
		return DecodeArchiveName(path, 0u);
	}

	std::filesystem::path ImportFileSystem::ResolveDirectoryPath(std::string_view path) const {
		auto const root = m_entry.empty() ? m_source : m_source.parent_path();
		auto candidate = std::filesystem::weakly_canonical(
		    root / PathFromUTF8(NormalizeVirtualPath(path))
		);
		auto root_component = root.begin();
		auto resolved = candidate.begin();
		while (root_component != root.end() && resolved != candidate.end() &&
		       *root_component == *resolved) {
			++root_component;
			++resolved;
		}
		if (root_component != root.end()) {
			throw std::invalid_argument{"Virtual asset path escapes its import root"};
		}
		return candidate;
	}

	ImportFileSystem::ImportFileSystem(std::filesystem::path const& source) :
	    m_source(std::filesystem::weakly_canonical(source)),
	    m_entry(
	        std::filesystem::is_regular_file(m_source) && m_source.extension() != ".zip"
	            ? PathToUTF8(m_source.filename())
	            : std::string{}
	    ),
	    m_is_archive(
	        std::filesystem::is_regular_file(m_source) && m_source.extension() == ".zip"
	    ) {
		if (!m_is_archive && m_entry.empty() && !std::filesystem::is_directory(m_source)) {
			throw std::invalid_argument{"Import source must be a file, directory or ZIP archive"};
		}
	}

	bool ImportFileSystem::Exists(std::string_view path) const {
		if (!m_is_archive) {
			return std::filesystem::is_regular_file(ResolveDirectoryPath(path));
		}

		ArchiveHandle archive{m_source};
		return LocateArchiveFile(archive.Get(), path);
	}

		/**
		 * @brief Reads one import-relative file into owned memory.
		 * @param path Import-relative virtual path.
		 * @return Complete file contents.
		 * @throws std::runtime_error if the file cannot be found or read completely.
		 */
	std::vector<std::byte> ImportFileSystem::Read(std::string_view path) const {
			if (!m_is_archive) {
				auto const resolved = ResolveDirectoryPath(path);
				auto const size = std::filesystem::file_size(resolved);
				if (size > std::vector<std::byte>{}.max_size()) {
					throw std::length_error{"Import file is too large"};
				}
				std::vector<std::byte> result(static_cast<std::size_t>(size));
				std::ifstream input{resolved, std::ios::binary};
				if (!input || !input.read(reinterpret_cast<char*>(result.data()), result.size())) {
					throw std::runtime_error{"Failed to read import file"};
				}
				return result;
			}

			ArchiveHandle archive{m_source};
			if (!LocateArchiveFile(archive.Get(), path)) {
				throw std::runtime_error{"File does not exist in asset archive"};
			}

			unz_file_info64 information{};
			if (unzGetCurrentFileInfo64(
			        archive.Get(),
			        &information,
			        nullptr,
			        0,
			        nullptr,
			        0,
			        nullptr,
			        0
			    ) != UNZ_OK) {
				throw std::runtime_error{"Failed to inspect file in asset archive"};
			}
			if (information.uncompressed_size > std::vector<std::byte>{}.max_size()) {
				throw std::length_error{"Archived import file is too large"};
			}

			ArchiveEntry entry{archive.Get()};
			std::vector<std::byte> result(static_cast<std::size_t>(information.uncompressed_size));
			std::size_t offset = 0;
			while (offset != result.size()) {
				auto const remaining = std::min(
				    result.size() - offset,
				    static_cast<std::size_t>(std::numeric_limits<unsigned int>::max())
				);
				auto const read = unzReadCurrentFile(
				    archive.Get(),
				    result.data() + offset,
				    static_cast<unsigned int>(remaining)
				);
				if (read <= 0) {
					throw std::runtime_error{"Failed to read file from asset archive"};
				}
				offset += static_cast<std::size_t>(read);
			}
			return result;
	}

		/**
		 * @brief Enumerates every regular file visible through the import filesystem.
		 * @return Import-relative paths accepted by Exists() and Read().
		 * @note Returned virtual paths always use validated UTF-8 and forward slashes.
		 */
	std::vector<std::string> ImportFileSystem::ListFiles() const {
			std::vector<std::string> result;
			if (!m_entry.empty()) {
				result.emplace_back(m_entry);
				return result;
			}
			if (!m_is_archive) {
				for (auto const& entry : std::filesystem::recursive_directory_iterator{m_source}) {
					if (entry.is_regular_file()) {
						result.emplace_back(
						    PathToUTF8(std::filesystem::relative(entry.path(), m_source))
						);
					}
				}
				return result;
			}

			ArchiveHandle archive{m_source};
			auto status = unzGoToFirstFile(archive.Get());
			while (status == UNZ_OK) {
				auto [raw_name, information] = CurrentArchiveName(archive.Get());
				if (!raw_name.empty() && raw_name.back() != '/' && raw_name.back() != '\\') {
					result.emplace_back(NormalizeVirtualPath(
					    DecodeArchiveName(raw_name, information.flag)
					));
				}
				status = unzGoToNextFile(archive.Get());
			}
			if (status != UNZ_END_OF_LIST_OF_FILE) {
				throw std::runtime_error{"Failed to enumerate asset archive"};
			}
			return result;
	}

} // namespace fyuu_asset
