module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>

#include <string_view>

#include <filesystem>
#endif // !defined(__cpp_lib_modules)

module fyuu_asset:context;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :uuid;

namespace fyuu_asset {

	/** @brief Identity metadata retained by the internal asset catalog. */
	struct CatalogEntry final {
		UUID m_uuid;        ///< Globally unique asset identifier.
		std::string m_type; ///< Runtime asset type name.
		std::string m_name; ///< Name unique within the asset type.
	};

	/**
	 * @brief Returns the initialized asset root for internal operations.
	 * @return Immutable normalized asset root.
	 * @throws std::logic_error if the asset context has not been initialized.
	 */
	[[nodiscard]] std::filesystem::path const& GetAssetRoot();

	/**
	 * @brief Finds and copies catalog identity metadata by UUID.
	 * @param uuid Stable asset UUID.
	 * @return Catalog identity metadata.
	 * @throws std::logic_error if the asset context has not been initialized.
	 * @throws std::out_of_range if the UUID is not present in the catalog.
	 */
	[[nodiscard]] CatalogEntry FindCatalogEntry(UUID const& uuid);

	/**
	 * @brief Finds and copies catalog identity metadata by type and name.
	 * @param type Runtime asset type name.
	 * @param name Asset name unique within @p type.
	 * @return Catalog identity metadata.
	 * @throws std::logic_error if the asset context has not been initialized.
	 * @throws std::out_of_range if the pair is not present in the catalog.
	 */
	[[nodiscard]] CatalogEntry FindCatalogEntry(
	    std::string_view type,
	    std::string_view name
	);

	/**
	 * @brief Validates that one new identity can be inserted without conflicts.
	 * @param entry Identity metadata intended for a new asset.
	 * @throws std::invalid_argument if its UUID or type-and-name pair already exists.
	 */
	void ValidateNewCatalogEntry(CatalogEntry const& entry);

	/**
	 * @brief Inserts or validates one asset identity in the internal catalog.
	 * @param entry Identity metadata to publish after persistence succeeds.
	 * @throws std::invalid_argument if the UUID or type-and-name pair conflicts.
	 */
	void RegisterCatalogEntry(CatalogEntry const& entry);

	/**
	 * @brief Removes one asset identity and immediately persists the catalog.
	 * @param uuid Stable UUID of the asset to remove.
	 * @throws std::logic_error if the asset context has not been initialized.
	 * @throws std::out_of_range if the UUID is not present in the catalog.
	 */
	void RemoveCatalogEntry(UUID const& uuid);

} // namespace fyuu_asset
