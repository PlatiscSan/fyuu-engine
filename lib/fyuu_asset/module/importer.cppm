module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>

#include <string_view>

#include <filesystem>

#include <coroutine>
#endif // !defined(__cpp_lib_modules)

export module fyuu_asset:importer;
import :import_file_system;
import :operation;
import :value;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_asset {
	/** @brief Describes the parser assigned to one open runtime asset type. */
	struct ImportProtocol final {
		/**
		 * @brief Parses one virtual import filesystem into the type's complete fields.
		 *
		 * The callback executes on the asset worker and may throw to fail the import.
		 * The filesystem defines the complete source scope. The parser discovers its
		 * entry points through ImportFileSystem::ListFiles() and reads source files and
		 * their dependencies through ImportFileSystem::Read().
		 */
		Object (*Parse)(ImportFileSystem const& file_system) = nullptr;
	};

	/** @brief Asynchronously parses one source into a caller-transformable dynamic object. */
	class Importer final {
	private:
		OperationTask m_task;           ///< Queue node embedded in this awaitable.
		std::filesystem::path m_source; ///< Source path owned by this operation.
		ImportProtocol m_protocol;      ///< Type protocol copied before suspension.
		Object m_result;                ///< Neutral fields produced by the parser.
		std::exception_ptr m_error;     ///< Exception produced by the worker.

		/** @brief Executes parsing on the shared asset worker. */
		static void Execute(OperationTask* submitted) noexcept;

	public:
		/**
		 * @brief Registers an application-defined parser protocol for one runtime type.
		 * @param type Open runtime asset type name.
		 * @param protocol Parser used to produce the type's complete dynamic fields.
		 * @throws std::invalid_argument if the type is invalid or already registered.
		 */
		static void Register(std::string_view type, ImportProtocol const& protocol);

		/**
		 * @brief Registers FyuuAsset's format-detecting parser for one runtime type.
		 *
		 * @details The internal parser selects behavior from file signatures and, where necessary, the source
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
		 * @par Built-in model field schema
		 * The following pseudo-structures describe Object member names and their Value
		 * alternatives. `Array<T>` and `Optional<T>` are documentation notation, not
		 * additional public C++ types.
		 *
		 * @code{.cpp}
		 * struct ByteRange {
		 *     std::uint64_t offset; ///< Byte offset from the beginning of the owning File.
		 *     std::uint64_t size;   ///< Number of bytes occupied by this range.
		 * };
		 *
		 * struct MeshFields {
		 *     Optional<std::string> name;       ///< Present on Assimp meshes.
		 *     std::uint64_t material;            ///< Index into ModelFields::materials.
		 *     std::uint64_t vertex_count;
		 *     std::uint64_t index_count;
		 *     ByteRange positions;               ///< float32[vertex_count][3].
		 *     Optional<ByteRange> normals;       ///< float32[vertex_count][3].
		 *     Optional<ByteRange> texcoords;     ///< float32[vertex_count][2].
		 *     Optional<ByteRange> tangents;      ///< float32[vertex_count][3].
		 *     Optional<ByteRange> bitangents;    ///< float32[vertex_count][3].
		 *     Optional<ByteRange> bone_indices; ///< PMD: uint32[vertex_count][2].
		 *     Optional<ByteRange> bone_weights; ///< PMD: float32[vertex_count][2].
		 *     ByteRange indices;                 ///< uint32[index_count].
		 * };
		 *
		 * struct TextureBindingFields {
		 *     std::uint64_t semantic; ///< Numeric Assimp aiTextureType value.
		 *     std::uint64_t texture;  ///< Index into ModelFields::textures.
		 * };
		 *
		 * struct AssimpMaterialFields {
		 *     std::string name;
		 *     Array<TextureBindingFields> textures;
		 * };
		 *
		 * struct PMDMaterialFields {
		 *     Array<double, 3> diffuse;
		 *     double alpha;
		 *     double specularity;
		 *     Array<double, 3> specular;
		 *     Array<double, 3> ambient;
		 *     Array<TextureBindingFields> textures;
		 * };
		 *
		 * struct MipLevelFields {
		 *     std::uint64_t width;
		 *     std::uint64_t height;
		 *     std::uint64_t offset; ///< Byte offset into TextureFields::pixels.
		 *     std::uint64_t size;   ///< Compressed byte count for this mip level.
		 * };
		 *
		 * struct TextureFields {
		 *     std::string name;                 ///< Original logical texture name.
		 *     std::uint64_t width;              ///< Base mip width.
		 *     std::uint64_t height;             ///< Base mip height.
		 *     std::string format;               ///< One of the BC format strings below.
		 *     Array<MipLevelFields> mip_levels; ///< Ordered from base mip through 1x1.
		 *     File pixels;                      ///< Concatenated BC mip data, named *.texture.
		 * };
		 *
		 * struct ModelFields {
		 *     File geometry;                    ///< Named geometry.mesh.
		 *     Array<MeshFields> meshes;
		 *     Array<AssimpMaterialFields | PMDMaterialFields> materials;
		 *     Array<TextureFields> textures;
		 *     std::uint64_t animation_count;    ///< PMD fallback writes zero.
		 * };
		 *
		 * struct MultipleModelFields {
		 *     struct NamedModelFields : ModelFields {
		 *         std::string name; ///< Generated as model_0, model_1, ... in entry order.
		 *     };
		 *     Array<NamedModelFields> models;
		 * };
		 *
		 * struct BinaryFallbackFields {
		 *     Binary binary; ///< Complete source bytes without conversion.
		 * };
		 * @endcode
		 *
		 * Every MeshFields ByteRange addresses ModelFields::geometry. `material` and
		 * TextureBindingFields::texture are local array indexes within the same
		 * ModelFields Object; neither is a UUID or remains meaningful after extracting
		 * the referenced element from its model. Optional members are omitted rather
		 * than stored as null. PMX texture coordinates have Assimp's format-specific
		 * V-axis flip reversed during import.
		 *
		 * Source images are decoded to RGBA8 and mipmapped through 1x1 before compression.
		 * TextureFields::format is `bc1_rgba_unorm`, `bc1_rgba_unorm_srgb`,
		 * `bc3_rgba_unorm`, `bc3_rgba_unorm_srgb` or `bc5_rg_unorm`. BC3 is selected
		 * when any source texel has non-opaque alpha, BC5 for normal-map semantics, and
		 * `_srgb` for color semantics. A package with one entry returns ModelFields
		 * directly; multiple model entries return MultipleModelFields. A source that is
		 * neither JSON nor an importable model returns BinaryFallbackFields.
		 *
		 * @note RegisterBuiltIn() assigns the parser described above. A custom protocol
		 * may produce an entirely different Object schema from the same virtual files.
		 * @param type Open runtime asset type name.
		 * @throws std::invalid_argument if the type is invalid or already registered.
		 */
		static void RegisterBuiltIn(std::string_view type);

		/**
		 * @brief Creates an import operation using one registered runtime type protocol.
		 * @param type Open runtime asset type name.
		 * @param source Regular source file, import directory or ZIP package.
		 * @throws std::out_of_range if @p type has no registered protocol.
		 */
		Importer(std::string_view type, std::filesystem::path const& source);

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
