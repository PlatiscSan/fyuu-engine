module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>

#include <string>

#include <fstream>

#include <atomic>
#include <mutex>

#include <system_error>

#include <string_view>

#include <filesystem>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif // !defined(NOMINMAX)
#include <Windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#else
#error FyuuAsset catalog persistence is not implemented for this platform
#endif // defined(_WIN32)
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/tag.hpp>
#include <boost/multi_index_container.hpp>
#include <boost/tuple/tuple.hpp>
#include <nlohmann/json.hpp>

module fyuu_asset;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :context;
import :core;
import :uuid;

namespace {
	struct ByUUID {}; ///< Selects the UUID catalog index.
	struct ByName {}; ///< Selects the type-and-name catalog index.

	/** @brief Hashes a 16-byte asset identity for the catalog UUID index. */
	struct UUIDHash final {
		/**
		 * @brief Hashes every identity byte.
		 * @param uuid Asset identity bytes.
		 * @return Hash value used by the catalog lookup index.
		 */
		[[nodiscard]] std::size_t operator()(fyuu_asset::UUID const& uuid) const noexcept {
			auto hash = static_cast<std::size_t>(14695981039346656037ull);
			for (auto const byte : uuid) {
				hash ^= std::to_integer<std::size_t>(byte);
				hash *= static_cast<std::size_t>(1099511628211ull);
			}
			return hash;
		}
	};

	/** @brief Single catalog storage with two unique hashed lookup indexes. */
	using Catalog = boost::multi_index_container<
	    fyuu_asset::CatalogEntry,
	    boost::multi_index::indexed_by<
	        boost::multi_index::hashed_unique<
	            boost::multi_index::tag<ByUUID>,
	            boost::multi_index::member<
	                fyuu_asset::CatalogEntry,
	                fyuu_asset::UUID,
	                &fyuu_asset::CatalogEntry::m_uuid>,
	            UUIDHash>,
	        boost::multi_index::hashed_unique<
	            boost::multi_index::tag<ByName>,
	            boost::multi_index::composite_key<
	                fyuu_asset::CatalogEntry,
	                boost::multi_index::member<
	                    fyuu_asset::CatalogEntry,
	                    std::string,
	                    &fyuu_asset::CatalogEntry::m_type>,
	                boost::multi_index::member<
	                    fyuu_asset::CatalogEntry,
	                    std::string,
	                    &fyuu_asset::CatalogEntry::m_name>>>>>;

	std::once_flag s_initialize_once; ///< Serializes the one permitted context initialization.
	std::mutex s_catalog_mutex; ///< Serializes catalog lookup, insertion and persistence.
	std::atomic<fyuu_asset::log::Sink*> s_log_sink =
	    nullptr;                        ///< Borrowed process-wide diagnostic destination.
	std::filesystem::path s_asset_root; ///< Immutable normalized root after initialization.
	Catalog s_catalog; ///< Deserialized catalog.json and its immutable lookup indexes.

#if defined(__unix__) || defined(__APPLE__)
	/** @brief Closes one POSIX descriptor while preserving an earlier operation error. */
	void CloseDescriptor(int descriptor, int operation_error, char const* operation) {
		auto const close_result = ::close(descriptor);
		if (operation_error != 0) {
			throw std::system_error{operation_error, std::generic_category(), operation};
		}
		if (close_result != 0) {
			throw std::system_error{errno, std::generic_category(), "Failed to close catalog storage"};
		}
	}

	/** @brief Flushes one catalog file through the POSIX storage boundary. */
	void FlushCatalogFile(std::filesystem::path const& path) {
		auto const descriptor = ::open(path.c_str(), O_RDONLY);
		if (descriptor == -1) {
			throw std::system_error{errno, std::generic_category(), "Failed to open asset catalog"};
		}
#if defined(__APPLE__)
		auto const result = ::fcntl(descriptor, F_FULLFSYNC);
#else
		auto const result = ::fsync(descriptor);
#endif // defined(__APPLE__)
		auto const operation_error = result == 0 ? 0 : errno;
		CloseDescriptor(descriptor, operation_error, "Failed to flush asset catalog");
	}

	/** @brief Flushes the parent directory containing a newly replaced catalog. */
	void FlushCatalogDirectory(std::filesystem::path const& path) {
		auto const descriptor = ::open(path.c_str(), O_RDONLY);
		if (descriptor == -1) {
			throw std::system_error{
			    errno,
			    std::generic_category(),
			    "Failed to open the asset catalog directory"
			};
		}
		auto const result = ::fsync(descriptor);
		auto const operation_error = result == 0 ? 0 : errno;
		CloseDescriptor(
		    descriptor,
		    operation_error,
		    "Failed to flush the asset catalog directory"
		);
	}
#endif // defined(__unix__) || defined(__APPLE__)

	/** @brief Rejects an identity that conflicts with the catalog while its mutex is held. */
	void ValidateNewCatalogEntryLocked(fyuu_asset::CatalogEntry const& entry) {
		auto const& by_uuid = s_catalog.get<ByUUID>();
		if (by_uuid.contains(entry.m_uuid)) {
			throw std::invalid_argument{"Asset UUID conflicts with the catalog"};
		}
		for (auto const& existing : s_catalog)
			if (existing.m_name == entry.m_name)
				throw std::invalid_argument{"Asset name conflicts with the workspace directory"};
	}

	/**
	 * @brief Loads catalog.json and constructs both lookup indexes.
	 * @param asset_root Normalized asset root containing catalog.json.
	 * @return Fully indexed catalog, or an empty catalog when the file does not exist.
	 */
	[[nodiscard]] Catalog LoadCatalog(std::filesystem::path const& asset_root) {
		auto const path = asset_root / "catalog.json";
		if (!std::filesystem::exists(path)) {
			return {};
		}

		std::ifstream input{path, std::ios::binary};
		if (!input) {
			throw std::runtime_error{"Failed to open asset catalog"};
		}

		auto const document = nlohmann::json::parse(input);
		auto const& assets = document.at("assets");
		if (!assets.is_array()) {
			throw std::invalid_argument{"Asset catalog field 'assets' must be an array"};
		}

		Catalog catalog;
		for (auto const& asset : assets) {
			fyuu_asset::CatalogEntry entry{
			    .m_uuid = fyuu_asset::ParseUUID(asset.at("uuid").get<std::string>()),
			    .m_type = asset.at("type").get<std::string>(),
			    .m_name = asset.at("name").get<std::string>()
			};
			if (entry.m_uuid == fyuu_asset::NilUUID()) {
				throw std::invalid_argument{"Asset catalog UUID must not be nil"};
			}
			if (entry.m_type.empty() || entry.m_name.empty()) {
				throw std::invalid_argument{"Asset catalog type and name must not be empty"};
			}

			auto const& by_uuid = catalog.get<ByUUID>();
			if (by_uuid.contains(entry.m_uuid)) {
				throw std::invalid_argument{"Asset catalog contains a duplicate UUID"};
			}
			auto const& by_name = catalog.get<ByName>();
			if (by_name.contains(boost::make_tuple(entry.m_type, entry.m_name))) {
				throw std::invalid_argument{"Asset catalog contains a duplicate type and name"};
			}
			catalog.insert(std::move(entry));
		}
		return catalog;
	}

	/**
	 * @brief Atomically replaces catalog.json while the catalog mutex is held.
	 * @param committed Receives whether the namespace replacement already occurred.
	 */
	void SerializeCatalogLocked(bool& committed) {
		committed = false;
		std::filesystem::create_directories(s_asset_root);
		nlohmann::json assets = nlohmann::json::array();
		for (auto const& entry : s_catalog) {
			assets.push_back(
			    {{"uuid", fyuu_asset::FormatUUID(entry.m_uuid)},
				 {"type", entry.m_type},
				 {"name", entry.m_name}}
			);
		}

		auto const path = s_asset_root / "catalog.json";
		auto temporary = path;
		temporary += ".tmp";
		std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
		if (!output) {
			throw std::runtime_error{"Failed to open asset catalog for writing"};
		}
		output << nlohmann::json{{"assets", std::move(assets)}}.dump(2);
		output.flush();
		if (!output) {
			throw std::runtime_error{"Failed to write asset catalog"};
		}
		output.close();

#if defined(_WIN32)
		auto const file = CreateFileW(
		    temporary.c_str(),
		    GENERIC_WRITE,
		    FILE_SHARE_READ,
		    nullptr,
		    OPEN_EXISTING,
		    FILE_ATTRIBUTE_NORMAL,
		    nullptr
		);
		if (file == INVALID_HANDLE_VALUE) {
			throw std::runtime_error{"Failed to reopen the temporary asset catalog"};
		}
		auto const flushed = FlushFileBuffers(file) != FALSE;
		CloseHandle(file);
		if (!flushed) {
			throw std::runtime_error{"Failed to flush the temporary asset catalog"};
		}
		auto const destination_exists = std::filesystem::exists(path);
		if (destination_exists && !ReplaceFileW(
		                              path.c_str(),
		                              temporary.c_str(),
		                              nullptr,
		                              REPLACEFILE_WRITE_THROUGH,
		                              nullptr,
		                              nullptr
		                          )) {
			throw std::system_error{
			    static_cast<int>(GetLastError()),
			    std::system_category(),
			    "Failed to replace the asset catalog"
			};
		}
		if (!destination_exists) {
			std::filesystem::rename(temporary, path);
		}
		committed = true;
#elif defined(__unix__) || defined(__APPLE__)
		FlushCatalogFile(temporary);
		std::filesystem::rename(temporary, path);
		committed = true;
		FlushCatalogDirectory(path.parent_path());
#endif // defined(_WIN32)
	}
} // namespace

namespace fyuu_asset {

	std::filesystem::path const& GetAssetRoot() {
		if (s_asset_root.empty()) {
			throw std::logic_error{"Asset context has not been initialized"};
		}
		return s_asset_root;
	}

	CatalogEntry FindCatalogEntry(UUID const& uuid) {
		if (s_asset_root.empty()) {
			throw std::logic_error{"Asset context has not been initialized"};
		}
		std::lock_guard lock{s_catalog_mutex};
		auto const& by_uuid = s_catalog.get<ByUUID>();
		auto const found = by_uuid.find(uuid);
		if (found == by_uuid.end()) {
			throw std::out_of_range{"Asset UUID is not present in the catalog"};
		}
		return *found;
	}

	CatalogEntry FindCatalogEntry(std::string_view type, std::string_view name) {
		if (s_asset_root.empty()) {
			throw std::logic_error{"Asset context has not been initialized"};
		}
		std::lock_guard lock{s_catalog_mutex};
		auto const& by_name = s_catalog.get<ByName>();
		auto const found = by_name.find(
		    boost::make_tuple(std::string{type}, std::string{name})
		);
		if (found == by_name.end()) {
			throw std::out_of_range{"Asset type and name are not present in the catalog"};
		}
		return *found;
	}

	void ValidateNewCatalogEntry(CatalogEntry const& entry) {
		if (s_asset_root.empty()) {
			throw std::logic_error{"Asset context has not been initialized"};
		}
		std::lock_guard lock{s_catalog_mutex};
		ValidateNewCatalogEntryLocked(entry);
	}

	void RegisterCatalogEntry(CatalogEntry const& entry) {
		if (s_asset_root.empty()) {
			throw std::logic_error{"Asset context has not been initialized"};
		}
		std::lock_guard lock{s_catalog_mutex};
		auto const& by_uuid = s_catalog.get<ByUUID>();
		if (auto const found = by_uuid.find(entry.m_uuid); found != by_uuid.end()) {
			if (found->m_type != entry.m_type || found->m_name != entry.m_name) {
				throw std::invalid_argument{"Asset UUID conflicts with the catalog"};
			}
			return;
		}
		ValidateNewCatalogEntryLocked(entry);
		s_catalog.insert(entry);
		auto committed = false;
		try {
			SerializeCatalogLocked(committed);
		} catch (...) {
			if (!committed) {
				s_catalog.get<ByUUID>().erase(entry.m_uuid);
			}
			throw;
		}
	}

	void RemoveCatalogEntry(UUID const& uuid) {
		if (s_asset_root.empty()) {
			throw std::logic_error{"Asset context has not been initialized"};
		}
		std::lock_guard lock{s_catalog_mutex};
		auto& by_uuid = s_catalog.get<ByUUID>();
		auto const found = by_uuid.find(uuid);
		if (found == by_uuid.end()) {
			throw std::out_of_range{"Asset UUID is not present in the catalog"};
		}
		auto const removed = *found;
		by_uuid.erase(found);
		auto committed = false;
		try {
			SerializeCatalogLocked(committed);
		} catch (...) {
			if (!committed) {
				s_catalog.insert(removed);
			}
			throw;
		}
	}

	void InitializeAssetContext(std::filesystem::path const& asset_root, log::Sink* sink) {
		if (asset_root.empty()) {
			throw std::invalid_argument{"Asset root must not be empty"};
		}

		std::call_once(s_initialize_once, [&asset_root, sink]() {
			s_asset_root = std::filesystem::absolute(asset_root).lexically_normal();
			s_catalog = LoadCatalog(s_asset_root);
			if (!std::filesystem::exists(s_asset_root / "catalog.json")) {
				auto committed = false;
				SerializeCatalogLocked(committed);
			}
			s_log_sink.store(sink, std::memory_order::release);
		});
	}

} // namespace fyuu_asset
