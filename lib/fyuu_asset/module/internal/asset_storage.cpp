module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <utility>

#include <string>

#include <bit>

#include <string_view>

#include <filesystem>
#endif // !defined(__cpp_lib_modules)
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid.hpp>

module fyuu_asset:asset_storage;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :context;
import :serialization;
import :uuid;
import :value;

namespace fyuu_asset {
	/** @brief Identity and fields read from persistent asset storage. */
	struct LoadedAssetData final {
		UUID m_uuid;     ///< Stable identity requested by the caller.
		std::string m_type; ///< Runtime type copied from the catalog.
		std::string m_name; ///< Asset name copied from the catalog.
		Object m_fields; ///< Dynamic fields read from persistent storage.
	};

	/** @brief Validates one dynamic identity component used in an asset path. */
	void ValidateAssetPathComponent(std::string_view component) {
		static_cast<void>(FromVirtualPath(component));
		if (component.empty() || component == "." || component == ".." ||
		    component.find_first_of("<>:\"/\\|?*") != std::string_view::npos ||
		    component.back() == '.' || component.back() == ' ') {
			throw std::invalid_argument{"Asset type and name must be safe path components"};
		}
		for (auto const character : component) {
			if (static_cast<unsigned char>(character) < 0x20u) {
				throw std::invalid_argument{"Asset type and name must be safe path components"};
			}
		}
	}

	/** @brief Builds the persistent JSON path for one typed asset. */
	[[nodiscard]] std::filesystem::path AssetStoragePath(
	    std::string_view type,
	    std::string_view name
	) {
		ValidateAssetPathComponent(type);
		ValidateAssetPathComponent(name);
		return GetAssetRoot() / FromVirtualPath(name) / "asset.json";
	}

	/** @brief Generates a non-nil identity for a newly created asset. */
	[[nodiscard]] UUID GenerateAssetUUID() {
		boost::uuids::random_generator Generate;
		auto uuid = Generate();
		while (uuid.is_nil()) {
			uuid = Generate();
		}
		return std::bit_cast<UUID>(uuid);
	}

	/** @brief Validates one dynamically typed asset identity. */
	void ValidateAssetIdentity(UUID const& uuid, std::string_view type, std::string_view name) {
		if (uuid == UUID{}) {
			throw std::invalid_argument{"Asset UUID must not be nil"};
		}
		if (type.empty() || name.empty()) {
			throw std::invalid_argument{"Asset type and name must not be empty"};
		}
		if (type.contains('\0') || name.contains('\0')) {
			throw std::invalid_argument{"Asset type and name must not contain null characters"};
		}
		ValidateAssetPathComponent(type);
		ValidateAssetPathComponent(name);
	}

	/** @brief Reads catalog identity and persistent fields for one asset. */
	[[nodiscard]] LoadedAssetData LoadAssetData(CatalogEntry&& identity) {
		auto path = AssetStoragePath(identity.m_type, identity.m_name);
		if (!std::filesystem::exists(path)) {
			path = GetAssetRoot() / FromVirtualPath(identity.m_type) /
			    FromVirtualPath(identity.m_name + ".json");
		}
		return LoadedAssetData{
		    .m_uuid = identity.m_uuid,
		    .m_type = std::move(identity.m_type),
		    .m_name = std::move(identity.m_name),
		    .m_fields = DeserializeAssetFields(path)
		};
	}

	[[nodiscard]] LoadedAssetData LoadAssetData(UUID const& uuid) {
		return LoadAssetData(FindCatalogEntry(uuid));
	}

	[[nodiscard]] LoadedAssetData LoadAssetData(std::string_view type, std::string_view name) {
		return LoadAssetData(FindCatalogEntry(type, name));
	}
} // namespace fyuu_asset
