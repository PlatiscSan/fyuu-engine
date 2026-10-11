module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <utility>

#include <string_view>

#include <filesystem>

#include <coroutine>
#endif // !defined(__cpp_lib_modules)

export module fyuu_asset:importer;
import :operation;
import :value;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_asset {
	/** @brief Parses one source into the complete dynamic fields chosen by an asset type. */
	using ImportFunction = Object (*)(std::filesystem::path const& source);

	/** @brief Asynchronously parses one source into a caller-transformable dynamic object. */
	class Importer final {
	private:
		OperationTask m_task;           ///< Queue node embedded in this awaitable.
		std::filesystem::path m_source; ///< Source path owned by this operation.
		ImportFunction Import;          ///< Parser selected from the runtime type registry.
		Object m_result;                ///< Neutral fields produced by the parser.
		std::exception_ptr m_error;     ///< Exception produced by the worker.

		/** @brief Executes parsing on the shared asset worker. */
		static void Execute(OperationTask* submitted) noexcept;

	public:
		/**
		 * @brief Starts the importer registered for one runtime asset type.
		 * @param type Runtime type used to select the importer.
		 * @param source Source whose contents are owned by the resulting operation.
		 * @throws std::out_of_range if @p type has no registered importer.
		 */
		Importer(std::string_view type, std::filesystem::path const& source);

		/**
		 * @brief Registers an application-defined importer for one runtime type.
		 * @param type Runtime asset type name.
		 * @param Import Parser executed asynchronously by Importer.
		 * @throws std::invalid_argument if the type or function is invalid or already registered.
		 */
		static void Register(std::string_view type, ImportFunction Import);

		/**
		 * @brief Registers FyuuAsset's format-detecting importer for one runtime type.
		 *
		 * @details RegisterBuiltIn() associates @p type with the following parser. The
		 * parser selects behavior from file signatures and, where necessary, the source
		 * extension. It currently recognizes JSON, PNG, JPEG, ZIP, GLB, FBX and PMD
		 * signatures and delegates other Assimp-supported model extensions to Assimp.
		 * A regular non-ZIP file is its own entry point. A directory or ZIP package uses
		 * `asset.json`: `entry` selects one UTF-8 virtual path and `entries` selects an
		 * array of UTF-8 virtual paths. Without that manifest, every supported model file
		 * is imported in sorted path order; when no model exists, exactly one JSON file is
		 * accepted. Ambiguous or empty packages are rejected.
		 *
		 * @par JSON result
		 * The JSON root must be an object and becomes the returned Object directly, so no
		 * built-in wrapper field is added. JSON null, Boolean, signed integer, unsigned
		 * integer, floating-point, string, array and object values map respectively to
		 * the corresponding Value alternatives. Member names and nesting are preserved.
		 *
		 * @par Model result
		 * One model produces these top-level fields:
		 * - `geometry` (`File`): named `geometry.mesh`; owns every packed vertex and index
		 *   stream referenced by the model.
		 * - `meshes` (`Array<Object>`): one element per imported mesh or PMD material range.
		 * - `materials` (`Array<Object>`): material descriptions indexed by mesh fields.
		 * - `textures` (`Array<Object>`): GPU-ready BC texture data indexed by material
		 *   bindings.
		 * - `animation_count` (`uint64`): source animation count reported by Assimp; the
		 *   PMD fallback importer writes zero because it does not import animation data.
		 *
		 * @par Mesh element
		 * Every mesh contains `material` (`uint64`, index into top-level `materials`),
		 * `vertex_count` (`uint64`) and `index_count` (`uint64`). Assimp meshes also contain
		 * `name` (`string`). Stream fields are present only when supplied by the source:
		 * `positions`, `normals`, `tangents` and `bitangents` are packed float32 triples;
		 * `texcoords` is packed float32 pairs; `indices` is packed uint32. PMD additionally
		 * supplies two uint32 values per vertex in `bone_indices` and two float32 values
		 * per vertex in `bone_weights`. PMX texture coordinates have their Assimp-specific
		 * V-axis flip reversed during import. Each stream field is an Object containing
		 * `offset` (`uint64`) and `size` (`uint64`) byte ranges into `geometry`.
		 *
		 * @par Material element
		 * Assimp materials contain `name` (`string`) and `textures` (`Array<Object>`).
		 * PMD fallback materials instead contain `diffuse` (three doubles), `alpha`
		 * (`double`), `specularity` (`double`), `specular` (three doubles), `ambient`
		 * (three doubles) and `textures`. Every texture binding contains `semantic`
		 * (`uint64`, the numeric Assimp aiTextureType value) and `texture` (`uint64`, an
		 * index into top-level `textures`). Consequently neither number is an asset UUID
		 * or a persistent reference outside its containing model Object.
		 *
		 * @par Texture element
		 * Source images are decoded to RGBA8, mipmapped to 1x1 and BC-compressed. Each
		 * texture contains `name` (`string`), `width` and `height` (`uint64`), `format`
		 * (`string`), `mip_levels` (`Array<Object>`) and `pixels` (`File`). `format` is
		 * `bc1_rgba_unorm`, `bc1_rgba_unorm_srgb`, `bc3_rgba_unorm`,
		 * `bc3_rgba_unorm_srgb` or `bc5_rg_unorm`; BC3 is selected when any source texel
		 * has non-opaque alpha, BC5 is selected for normal semantics, and the `_srgb`
		 * suffix is selected for color semantics. `pixels` is named from the source stem
		 * with a `.texture` extension and concatenates every compressed mip. Each
		 * `mip_levels` element contains `width`, `height`, `offset` and `size` (`uint64`),
		 * where the byte range addresses `pixels`.
		 *
		 * @par Multiple entries and fallback
		 * Multiple model entry points produce one top-level `models` (`Array<Object>`)
		 * instead of the single-model fields. Each element is a complete model Object and
		 * receives `name` equal to `model_0`, `model_1` and so on in entry order. A source
		 * that is neither JSON nor an importable model is preserved losslessly as
		 * `binary` (`Binary`).
		 *
		 * @note Field names describe the built-in importer's interchange contract only.
		 * Applications may register a different importer for another runtime type with
		 * Register().
		 * @param type Runtime asset type name.
		 * @throws std::invalid_argument if the type is invalid or already registered.
		 */
		static void RegisterBuiltIn(std::string_view type);

		Importer(Importer const&) = delete;
		Importer& operator=(Importer const&) = delete;
		Importer(Importer&&) = delete;
		Importer& operator=(Importer&&) = delete;

		/** @brief Always suspends so parsing executes on the asset worker. */
		[[nodiscard]] static constexpr bool await_ready() noexcept {
			return false;
		}

		/** @brief Submits this operation and stores its suspended continuation. */
		void await_suspend(std::coroutine_handle<> continuation);

		/** @brief Returns parser output or rethrows the worker exception. */
		[[nodiscard]] Object await_resume();
	};

} // namespace fyuu_asset
