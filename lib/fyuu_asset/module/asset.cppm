module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <utility>

#include <string>

#include <string_view>

#endif // !defined(__cpp_lib_modules)

export module fyuu_asset:asset;
import :uuid;
import :value;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_asset {
	class Factory;

	/**
	 * @brief Identifies one dynamically typed asset.
	 *
	 * Asset contains only the identity shared by every asset type. The runtime
	 * type named by GetType() owns the interpretation, loading and persistence
	 * of the asset's type-specific data.
	 */
	class Asset final {
	private:
		friend class Factory;

		UUID m_uuid;        ///< Stable identity persisted in the asset catalog.
		std::string m_type; ///< Runtime type used to select the asset implementation.
		std::string m_name; ///< Workspace-unique name of the independent asset directory.
		Object m_fields;    ///< Owned fields interpreted by the runtime asset type.
		bool m_owns_persistence = true; ///< Prevents moved-from objects from saving duplicate data.

		/**
		 * @brief Constructs an asset identity with an existing UUID.
		 * @param uuid Stable UUID read from the catalog or supplied by an importer.
		 * @param type Runtime asset type name.
		 * @param name Workspace-unique asset name; type does not classify its directory.
		 * @param fields Dynamic fields owned by the asset.
		 */
		Asset(UUID const& uuid, std::string_view type, std::string_view name, Object const& fields);

		/**
		 * @brief Constructs a loaded asset by taking ownership of its fields.
		 * @param uuid Stable UUID read from the catalog.
		 * @param type Runtime asset type name.
		 * @param name Workspace-unique asset name; type does not classify its directory.
		 * @param fields Loaded dynamic fields transferred into the asset.
		 */
		Asset(UUID const& uuid, std::string_view type, std::string_view name, Object&& fields);

		/**
		 * @brief Constructs a new asset identity with a generated UUID.
		 * @param type Runtime asset type name.
		 * @param name Workspace-unique asset name; type does not classify its directory.
		 * @param fields Dynamic fields owned by the asset.
		 */
		Asset(std::string_view type, std::string_view name, Object const& fields);

		/**
		 * @brief Constructs a new asset by taking ownership of imported fields.
		 * @param type Runtime asset type name.
		 * @param name Workspace-unique asset name; type does not classify its directory.
		 * @param fields Imported dynamic fields transferred into the asset.
		 */
		Asset(std::string_view type, std::string_view name, Object&& fields);

		/** @brief Detaches the current field snapshot for asynchronous persistence. */
		void QueuePersistence() noexcept;

	public:
		/** @brief Copies an asset identity and its current field snapshot. */
		Asset(Asset const& other);

		/** @brief Saves the current state and replaces it with a copied asset snapshot. */
		Asset& operator=(Asset const& other);

		/** @brief Transfers the asset value and its persistence responsibility. */
		Asset(Asset&& other) noexcept;

		/** @brief Saves the current state and takes ownership of another asset value. */
		Asset& operator=(Asset&& other) noexcept;

		/** @brief Queues the final asset state for asynchronous persistence. */
		~Asset() noexcept;

		/**
		 * @brief Returns the stable asset UUID.
		 * @return Stable UUID value owned by this asset.
		 */
		[[nodiscard]] UUID GetUUID() const noexcept;

		/**
		 * @brief Returns the runtime asset type name.
		 * @return Immutable runtime type name owned by this asset.
		 */
		[[nodiscard]] std::string_view GetType() const noexcept;

		/**
		 * @brief Returns the asset name.
		 * @return Immutable name owned by this asset.
		 */
		[[nodiscard]] std::string_view GetName() const noexcept;

		/**
		 * @brief Returns one dynamic field while preserving the asset value category.
		 * @param name Field name.
		 * @return Field value with constness and value category matching the asset.
		 * @throws std::out_of_range if the field does not exist.
		 */
		[[nodiscard]] decltype(auto) GetField(this auto&& self, std::string_view name) {
			auto const field = self.m_fields.find(name);
			if (field == self.m_fields.end()) {
				throw std::out_of_range{"Asset field does not exist"};
			}
			return std::forward_like<decltype(self)>(field->second);
		}
	};

} // namespace fyuu_asset
