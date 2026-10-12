module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>

#include <vector>

#include <algorithm>
#include <iterator>

#include <string>

#include <cstring>

#include <cstdint>
#include <type_traits>

#include <array>

#include <string_view>

#include <filesystem>

#include <span>

#include <bit>
#endif // !defined(__cpp_lib_modules)
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <stb_image_resize2.h>
#define STB_DXT_IMPLEMENTATION
#include <stb_dxt.h>
#include <assimp/Importer.hpp>
#include <assimp/IOStream.hpp>
#include <assimp/IOSystem.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <pmd_model.h>
#include <nlohmann/json.hpp>

module fyuu_asset:importer_impl;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :value;
import :import_file_system;
import :import_file_system_impl;

namespace fyuu_asset {

	namespace {
		constexpr std::size_t s_maximum_nesting = 256u;

		/** @brief Identifies source formats whose signatures can be determined reliably. */
		enum class SourceFormat { Unknown, JSON, PNG, JPEG, ZIP, GLB, FBX, PMD };

		/** @brief Compares one source byte with an unsigned byte literal. */
		[[nodiscard]] constexpr bool ByteEquals(std::byte value, unsigned int expected) noexcept {
			return std::to_integer<unsigned int>(value) == expected;
		}

		/**
		 * @brief Detects a source format from its file signature.
		 * @param bytes Complete source bytes.
		 * @return Reliably detected format, or SourceFormat::Unknown when content sniffing
		 * cannot distinguish the format.
		 * @note Text model formats such as OBJ do not have an unambiguous signature and
		 * therefore continue to use Assimp's extension probe after this function.
		 */
		[[nodiscard]] SourceFormat DetectSourceFormat(Binary const& bytes) noexcept {
			if (bytes.size() >= 8u && ByteEquals(bytes[0], 0x89u) && ByteEquals(bytes[1], 0x50u) &&
			    ByteEquals(bytes[2], 0x4eu) && ByteEquals(bytes[3], 0x47u) &&
			    ByteEquals(bytes[4], 0x0du) && ByteEquals(bytes[5], 0x0au) &&
			    ByteEquals(bytes[6], 0x1au) && ByteEquals(bytes[7], 0x0au)) {
				return SourceFormat::PNG;
			}
			if (bytes.size() >= 3u && ByteEquals(bytes[0], 0xffu) && ByteEquals(bytes[1], 0xd8u) &&
			    ByteEquals(bytes[2], 0xffu)) {
				return SourceFormat::JPEG;
			}
			if (bytes.size() >= 4u && ByteEquals(bytes[0], 0x50u) && ByteEquals(bytes[1], 0x4bu) &&
			    ((ByteEquals(bytes[2], 0x03u) && ByteEquals(bytes[3], 0x04u)) ||
			     (ByteEquals(bytes[2], 0x05u) && ByteEquals(bytes[3], 0x06u)) ||
			     (ByteEquals(bytes[2], 0x07u) && ByteEquals(bytes[3], 0x08u)))) {
				return SourceFormat::ZIP;
			}
			if (bytes.size() >= 4u && ByteEquals(bytes[0], 0x67u) && ByteEquals(bytes[1], 0x6cu) &&
			    ByteEquals(bytes[2], 0x54u) && ByteEquals(bytes[3], 0x46u)) {
				return SourceFormat::GLB;
			}
			if (bytes.size() >= 3u && ByteEquals(bytes[0], 0x50u) && ByteEquals(bytes[1], 0x6du) &&
			    ByteEquals(bytes[2], 0x64u)) {
				return SourceFormat::PMD;
			}

			constexpr std::string_view fbx_signature = "Kaydara FBX Binary";
			if (bytes.size() >= fbx_signature.size()) {
				bool matches = true;
				for (std::size_t index = 0u; index < fbx_signature.size(); ++index) {
					if (!ByteEquals(
					        bytes[index],
					        static_cast<unsigned char>(fbx_signature[index])
					    )) {
						matches = false;
						break;
					}
				}
				if (matches) {
					return SourceFormat::FBX;
				}
			}

			std::size_t offset = 0u;
			if (bytes.size() >= 3u && ByteEquals(bytes[0], 0xefu) && ByteEquals(bytes[1], 0xbbu) &&
			    ByteEquals(bytes[2], 0xbfu)) {
				offset = 3u;
			}
			while (offset < bytes.size()) {
				auto const character = std::to_integer<unsigned int>(bytes[offset]);
				if (character != 0x20u && character != 0x09u && character != 0x0au &&
				    character != 0x0du) {
					return character == static_cast<unsigned int>('{') ||
					        character == static_cast<unsigned int>('[') ?
					    SourceFormat::JSON :
					    SourceFormat::Unknown;
				}
				++offset;
			}
			return SourceFormat::Unknown;
		}

		/** @brief Returns a lowercase extension used only for ambiguous text formats. */
		[[nodiscard]] std::string LowercaseExtension(std::string_view entry) {
			auto extension = ToVirtualPath(FromVirtualPath(entry).extension());
			for (auto& character : extension) {
				if (character >= 'A' && character <= 'Z') {
					character = static_cast<char>(character - 'A' + 'a');
				}
			}
			return extension;
		}

		/** @brief Compares archive filenames without changing non-ASCII path bytes. */
		[[nodiscard]] bool EqualFilename(std::string_view left, std::string_view right) noexcept {
			if (left.size() != right.size()) {
				return false;
			}
			for (std::size_t index = 0u; index < left.size(); ++index) {
				auto left_character = left[index];
				auto right_character = right[index];
				if (left_character >= 'A' && left_character <= 'Z') {
					left_character = static_cast<char>(left_character - 'A' + 'a');
				}
				if (right_character >= 'A' && right_character <= 'Z') {
					right_character = static_cast<char>(right_character - 'A' + 'a');
				}
				if (left_character != right_character) {
					return false;
				}
			}
			return true;
		}

		/** @brief Returns the final component of one virtual path without transcoding it. */
		[[nodiscard]] std::string_view VirtualFilename(std::string_view path) noexcept {
			auto const separator = path.find_last_of("/\\");
			return separator == std::string_view::npos ? path : path.substr(separator + 1u);
		}

		/**
		 * @brief Relocates one model dependency into the import filesystem.
		 * @param file_system Import-scoped virtual filesystem.
		 * @param base Directory containing the model entry.
		 * @param dependency Path embedded in the source model.
		 * @return Existing import-relative virtual path.
		 */
		[[nodiscard]] std::string ResolveImportPath(
		    ImportFileSystem const& file_system,
		    std::string_view base,
		    std::string_view dependency
		) {
			auto const utf8_dependency = DecodeLegacyVirtualPath(dependency);
			auto embedded = FromVirtualPath(utf8_dependency);
			if (!embedded.is_absolute() && !embedded.has_root_name() &&
			    !embedded.has_root_directory()) {
				auto direct = ToVirtualPath(embedded);
				if (file_system.Exists(direct)) {
					return direct;
				}
				auto relative =
				    ToVirtualPath((FromVirtualPath(base) / embedded).lexically_normal());
				if (file_system.Exists(relative)) {
					return relative;
				}
			}

			auto const filename = VirtualFilename(utf8_dependency);
			for (auto& candidate : file_system.ListFiles()) {
				if (EqualFilename(VirtualFilename(candidate), filename)) {
					return std::move(candidate);
				}
			}
			throw std::runtime_error{
			    "Model dependency does not exist in the import package: " + std::string{dependency}
			};
		}

		/** @brief Read-only Assimp stream backed by one owned virtual file. */
		class AssimpIOStream final : public Assimp::IOStream {
		private:
			Binary m_bytes;
			std::size_t m_position = 0u;

		public:
			explicit AssimpIOStream(Binary&& bytes) : m_bytes(std::move(bytes)) {
			}

			std::size_t Read(
			    void* buffer,
			    std::size_t element_size,
			    std::size_t element_count
			) override {
				if (!buffer || element_size == 0u || element_count == 0u) {
					return 0u;
				}
				auto const available = (m_bytes.size() - m_position) / element_size;
				auto const count = std::min(available, element_count);
				auto const size = count * element_size;
				std::memcpy(buffer, m_bytes.data() + m_position, size);
				m_position += size;
				return count;
			}

			std::size_t Write(void const*, std::size_t, std::size_t) override {
				return 0u;
			}

			aiReturn Seek(std::size_t offset, aiOrigin origin) override {
				std::size_t position = 0u;
				switch (origin) {
					case aiOrigin_SET:
						position = offset;
						break;
					case aiOrigin_CUR:
						if (offset > m_bytes.size() - m_position) {
							return aiReturn_FAILURE;
						}
						position = m_position + offset;
						break;
					case aiOrigin_END: {
						auto const displacement = static_cast<std::ptrdiff_t>(offset);
						if (displacement > 0 ||
						    static_cast<std::size_t>(-displacement) > m_bytes.size()) {
							return aiReturn_FAILURE;
						}
						position = m_bytes.size() - static_cast<std::size_t>(-displacement);
						break;
					}
					default:
						return aiReturn_FAILURE;
				}
				if (position > m_bytes.size()) {
					return aiReturn_FAILURE;
				}
				m_position = position;
				return aiReturn_SUCCESS;
			}

			[[nodiscard]] std::size_t Tell() const override {
				return m_position;
			}

			[[nodiscard]] std::size_t FileSize() const override {
				return m_bytes.size();
			}

			void Flush() override {
			}
		};

		/** @brief Adapts the private import filesystem to Assimp dependency reads. */
		class AssimpIOSystem final : public Assimp::IOSystem {
		private:
			ImportFileSystem const& m_file_system;
			std::string m_base;

			/** @brief Resolves an Assimp path against the entry file directory. */
			[[nodiscard]] std::string Resolve(std::string_view path) const {
				return ResolveImportPath(m_file_system, m_base, path);
			}

		public:
			AssimpIOSystem(ImportFileSystem const& file_system, std::string_view entry) :
			    m_file_system(file_system),
			    m_base(ToVirtualPath(FromVirtualPath(entry).parent_path())) {
			}

			[[nodiscard]] bool Exists(char const* path) const override {
				if (!path) {
					return false;
				}
				try {
					return m_file_system.Exists(Resolve(path));
				} catch (...) {
					return false;
				}
			}

			[[nodiscard]] char getOsSeparator() const override {
				return '/';
			}

			Assimp::IOStream* Open(char const* path, char const* mode) override {
				if (!path || !mode || std::strchr(mode, 'w') || std::strchr(mode, 'a') ||
				    std::strchr(mode, '+')) {
					return nullptr;
				}
				try {
					return new AssimpIOStream{m_file_system.Read(Resolve(path))};
				} catch (...) {
					return nullptr;
				}
			}

			void Close(Assimp::IOStream* stream) override {
				delete stream;
			}
		};

		/** @brief Appends one unsigned 32-bit value using canonical little-endian bytes. */
		void AppendUInt32(Binary& output, std::uint32_t value) {
			output.push_back(static_cast<std::byte>(value & 0xffu));
			output.push_back(static_cast<std::byte>((value >> 8u) & 0xffu));
			output.push_back(static_cast<std::byte>((value >> 16u) & 0xffu));
			output.push_back(static_cast<std::byte>((value >> 24u) & 0xffu));
		}

		/** @brief Appends one IEEE-754 float using canonical little-endian bytes. */
		void AppendFloat(Binary& output, float value) {
			AppendUInt32(output, std::bit_cast<std::uint32_t>(value));
		}

		/** @brief Packs one Assimp vector stream without retaining Assimp memory. */
		[[nodiscard]] Binary PackVectors(aiVector3D const* vectors, std::size_t count) {
			Binary result;
			result.reserve(count * 3u * sizeof(float));
			for (std::size_t index = 0; index < count; ++index) {
				AppendFloat(result, vectors[index].x);
				AppendFloat(result, vectors[index].y);
				AppendFloat(result, vectors[index].z);
			}
			return result;
		}

		/**
		 * @brief Packs one Assimp texture-coordinate stream as two floats per vertex.
		 * @param coordinates Assimp texture-coordinate stream.
		 * @param count Number of texture coordinates.
		 * @param flip_v Whether to restore V after an importer-specific vertical flip.
		 * @return Packed float2 texture-coordinate bytes.
		 */
		[[nodiscard]] Binary PackTextureCoordinates(
		    aiVector3D const* coordinates,
		    std::size_t count,
		    bool flip_v
		) {
			Binary result;
			result.reserve(count * 2u * sizeof(float));
			for (std::size_t index = 0; index < count; ++index) {
				AppendFloat(result, coordinates[index].x);
				AppendFloat(result, flip_v ? 1.0f - coordinates[index].y : coordinates[index].y);
			}
			return result;
		}

		/** @brief Appends one stream and returns its byte range in the geometry file. */
		[[nodiscard]] Object AppendGeometryStream(Binary& geometry, Binary&& stream) {
			auto const offset = geometry.size();
			auto const size = stream.size();
			geometry.insert(
			    geometry.end(),
			    std::make_move_iterator(stream.begin()),
			    std::make_move_iterator(stream.end())
			);
			return Object{
			    {"offset", Value{static_cast<std::uint64_t>(offset)}},
			    {"size", Value{static_cast<std::uint64_t>(size)}}
			};
		}

		/** @brief Reports whether one Assimp texture semantic contains color data. */
		[[nodiscard]] bool IsSRGBTexture(aiTextureType type) noexcept {
			switch (type) {
				case aiTextureType_DIFFUSE:
				case aiTextureType_SPECULAR:
				case aiTextureType_AMBIENT:
				case aiTextureType_EMISSIVE:
				case aiTextureType_BASE_COLOR:
				case aiTextureType_EMISSION_COLOR:
					return true;
				default:
					return false;
			}
		}

		/** @brief Reports whether one Assimp texture semantic represents tangent-space normals. */
		[[nodiscard]] bool IsNormalTexture(aiTextureType type) noexcept {
			return type == aiTextureType_NORMALS || type == aiTextureType_NORMAL_CAMERA;
		}

		/** @brief Compresses one RGBA8 mip level into complete four-by-four BC blocks. */
		[[nodiscard]] Binary CompressBC(
		    unsigned char const* pixels,
		    std::size_t width,
		    std::size_t height,
		    bool alpha,
		    bool normal
		) {
			auto const block_width = (width + 3u) / 4u;
			auto const block_height = (height + 3u) / 4u;
			auto const block_size = alpha || normal ? 16u : 8u;
			Binary result(block_width * block_height * block_size);
			std::array<unsigned char, 4u * 4u * 4u> block{};
			std::array<unsigned char, 4u * 4u * 2u> normal_block{};
			for (std::size_t block_y = 0u; block_y < block_height; ++block_y) {
				for (std::size_t block_x = 0u; block_x < block_width; ++block_x) {
					for (std::size_t y = 0u; y < 4u; ++y) {
						for (std::size_t x = 0u; x < 4u; ++x) {
							auto const source_x = std::min(block_x * 4u + x, width - 1u);
							auto const source_y = std::min(block_y * 4u + y, height - 1u);
							auto const* source = pixels + (source_y * width + source_x) * 4u;
							std::memcpy(block.data() + (y * 4u + x) * 4u, source, 4u);
						}
					}
					auto* destination = reinterpret_cast<unsigned char*>(result.data()) +
					    (block_y * block_width + block_x) * block_size;
					if (normal) {
						for (std::size_t index = 0u; index < 16u; ++index) {
							normal_block[index * 2u] = block[index * 4u];
							normal_block[index * 2u + 1u] = block[index * 4u + 1u];
						}
						stb_compress_bc5_block(destination, normal_block.data());
					} else {
						stb_compress_dxt_block(
						    destination,
						    block.data(),
						    alpha ? 1 : 0,
						    STB_DXT_HIGHQUAL
						);
					}
				}
			}
			return result;
		}

		/** @brief Converts decoded RGBA8 pixels into a complete GPU-ready BC mip chain. */
		[[nodiscard]] Object CompressTexture(
		    std::string_view name,
		    unsigned char const* pixels,
		    std::size_t width,
		    std::size_t height,
		    bool srgb,
		    bool normal
		) {
			if (!pixels || width == 0u || height == 0u) {
				throw std::invalid_argument{"Imported texture has no pixels"};
			}
			auto const pixel_count = width * height;
			auto alpha = false;
			for (std::size_t index = 0u; index < pixel_count; ++index) {
				if (pixels[index * 4u + 3u] != 0xffu) {
					alpha = true;
					break;
				}
			}

			Binary compressed;
			Array mip_levels;
			auto mip_width = width;
			auto mip_height = height;
			std::vector<unsigned char> current(pixels, pixels + pixel_count * 4u);
			while (true) {
				auto encoded = CompressBC(current.data(), mip_width, mip_height, alpha, normal);
				auto const offset = compressed.size();
				compressed.insert(
				    compressed.end(),
				    std::make_move_iterator(encoded.begin()),
				    std::make_move_iterator(encoded.end())
				);
				mip_levels.emplace_back(
				    Object{
				        {"width", Value{static_cast<std::uint64_t>(mip_width)}},
				        {"height", Value{static_cast<std::uint64_t>(mip_height)}},
				        {"offset", Value{static_cast<std::uint64_t>(offset)}},
				        {"size", Value{static_cast<std::uint64_t>(encoded.size())}}
				    }
				);
				if (mip_width == 1u && mip_height == 1u) {
					break;
				}
				auto const next_width = std::max<std::size_t>(1u, mip_width / 2u);
				auto const next_height = std::max<std::size_t>(1u, mip_height / 2u);
				std::vector<unsigned char> next(next_width * next_height * 4u);
				auto const* resized = srgb ? stbir_resize_uint8_srgb(
				                                 current.data(),
				                                 static_cast<int>(mip_width),
				                                 static_cast<int>(mip_height),
				                                 0,
				                                 next.data(),
				                                 static_cast<int>(next_width),
				                                 static_cast<int>(next_height),
				                                 0,
				                                 STBIR_RGBA
				                             ) :
				                             stbir_resize_uint8_linear(
				                                 current.data(),
				                                 static_cast<int>(mip_width),
				                                 static_cast<int>(mip_height),
				                                 0,
				                                 next.data(),
				                                 static_cast<int>(next_width),
				                                 static_cast<int>(next_height),
				                                 0,
				                                 STBIR_RGBA
				                             );
				if (!resized) {
					throw std::runtime_error{"Failed to generate texture mip levels"};
				}
				current = std::move(next);
				mip_width = next_width;
				mip_height = next_height;
			}

			auto filename = ToVirtualPath(FromVirtualPath(name).stem()) + ".texture";
			return Object{
			    {"name", Value{std::string{name}}},
			    {"width", Value{static_cast<std::uint64_t>(width)}},
			    {"height", Value{static_cast<std::uint64_t>(height)}},
			    {"format",
				 Value{
			         normal ? std::string{"bc5_rg_unorm"} :
			                  std::string{alpha ? "bc3_rgba_unorm" : "bc1_rgba_unorm"} +
			                 (srgb ? "_srgb" : "")
			     }},
			    {"mip_levels", Value{std::move(mip_levels)}},
			    {"pixels",
				 Value{File{.m_name = std::move(filename), .m_data = std::move(compressed)}}}
			};
		}

		/** @brief Decodes one conventional image and converts it into a BC texture. */
		[[nodiscard]] Object ImportTexture(
		    std::string_view name,
		    Binary const& encoded,
		    bool srgb,
		    bool normal = false
		) {
			if (encoded.empty() || encoded.size() > static_cast<std::size_t>(INT_MAX)) {
				throw std::invalid_argument{"Imported texture has an invalid encoded size"};
			}
			int width = 0;
			int height = 0;
			int channels = 0;
			std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels{
			    stbi_load_from_memory(
			        reinterpret_cast<unsigned char const*>(encoded.data()),
			        static_cast<int>(encoded.size()),
			        &width,
			        &height,
			        &channels,
			        STBI_rgb_alpha
			    ),
			    &stbi_image_free
			};
			if (!pixels) {
				throw std::runtime_error{
				    "Failed to decode texture " + std::string{name} + ": " +
				    (stbi_failure_reason() ? stbi_failure_reason() : "unknown image error")
				};
			}
			return CompressTexture(
			    name,
			    pixels.get(),
			    static_cast<std::size_t>(width),
			    static_cast<std::size_t>(height),
			    srgb,
			    normal
			);
		}

		/** @brief Converts one Assimp embedded texture into the persistent BC representation. */
		[[nodiscard]] Object ImportTexture(
		    aiTexture const& texture,
		    std::string_view fallback_name,
		    bool srgb,
		    bool normal
		) {
			auto const name = texture.mFilename.length == 0u ?
			    fallback_name :
			    std::string_view{texture.mFilename.C_Str(), texture.mFilename.length};
			if (texture.mHeight == 0u) {
				Binary encoded(texture.mWidth);
				std::memcpy(encoded.data(), texture.pcData, encoded.size());
				return ImportTexture(name, encoded, srgb, normal);
			}
			std::vector<unsigned char> pixels(
			    static_cast<std::size_t>(texture.mWidth) * texture.mHeight * 4u
			);
			for (std::size_t index = 0u; index < pixels.size() / 4u; ++index) {
				pixels[index * 4u] = texture.pcData[index].r;
				pixels[index * 4u + 1u] = texture.pcData[index].g;
				pixels[index * 4u + 2u] = texture.pcData[index].b;
				pixels[index * 4u + 3u] = texture.pcData[index].a;
			}
			return CompressTexture(
			    name,
			    pixels.data(),
			    texture.mWidth,
			    texture.mHeight,
			    srgb,
			    normal
			);
		}

		/** @brief Bounds-checked reader for little-endian PMD memory. */
		class PMDReader final {
		private:
			std::span<std::byte const> m_bytes; ///< Complete PMD source bytes.
			std::size_t m_offset = 0u;          ///< Next unread byte.

		public:
			explicit PMDReader(Binary const& bytes) noexcept : m_bytes(bytes) {
			}

			/** @brief Reads one trivially copyable PMD value. */
			template <class T> [[nodiscard]] T Read() {
				static_assert(std::is_trivially_copyable_v<T>);
				if (sizeof(T) > m_bytes.size() - m_offset) {
					throw std::invalid_argument{"PMD source ends before its declared data"};
				}
				T result;
				std::memcpy(&result, m_bytes.data() + m_offset, sizeof(T));
				m_offset += sizeof(T);
				return result;
			}

			/** @brief Reads a bounded vector of trivially copyable PMD values. */
			template <class T> [[nodiscard]] std::vector<T> Read(std::size_t count) {
				static_assert(std::is_trivially_copyable_v<T>);
				if (count > (m_bytes.size() - m_offset) / sizeof(T)) {
					throw std::invalid_argument{"PMD source contains an invalid element count"};
				}
				std::vector<T> result(count);
				std::memcpy(result.data(), m_bytes.data() + m_offset, count * sizeof(T));
				m_offset += count * sizeof(T);
				return result;
			}
		};

		/** @brief Converts one fixed PMD byte string without reading beyond its field. */
		[[nodiscard]] std::string PMDString(char const* text, std::size_t capacity) {
			auto length = std::size_t{0u};
			while (length < capacity && text[length] != '\0') {
				++length;
			}
			return std::string{text, length};
		}

		/** @brief Creates one dynamic float vector. */
		[[nodiscard]] Value FloatVector(float const* values, std::size_t count) {
			Array result;
			result.reserve(count);
			for (std::size_t index = 0u; index < count; ++index) {
				result.emplace_back(static_cast<double>(values[index]));
			}
			return Value{std::move(result)};
		}

		/** @brief Imports one PMD model using MMDLoader's packed format declarations. */
		[[nodiscard]] Object ImportPMD(
		    ImportFileSystem const& file_system,
		    std::string_view entry,
		    Binary const& bytes
		) {
			static_assert(sizeof(mmd::PMDVertex) == 38u);
			static_assert(sizeof(mmd::PMDMaterial) == 70u);
			PMDReader reader{bytes};
			auto const magic = reader.Read<std::array<char, 3u>>();
			if (magic != std::array<char, 3u>{'P', 'm', 'd'}) {
				throw std::invalid_argument{"PMD source has an invalid signature"};
			}
			auto const version = reader.Read<float>();
			if (version != 1.0f) {
				throw std::invalid_argument{"PMD source has an unsupported version"};
			}
			[[maybe_unused]] auto const model_name = reader.Read<std::array<char, 20u>>();
			[[maybe_unused]] auto const model_comment = reader.Read<std::array<char, 256u>>();

			auto const vertex_count = reader.Read<std::uint32_t>();
			auto const vertices = reader.Read<mmd::PMDVertex>(vertex_count);
			auto const index_count = reader.Read<std::uint32_t>();
			auto const source_indices = reader.Read<std::uint16_t>(index_count);
			auto const material_count = reader.Read<std::uint32_t>();
			auto const source_materials = reader.Read<mmd::PMDMaterial>(material_count);

			Binary positions;
			Binary normals;
			Binary texcoords;
			Binary bone_indices;
			Binary bone_weights;
			positions.reserve(vertices.size() * 3u * sizeof(float));
			normals.reserve(vertices.size() * 3u * sizeof(float));
			texcoords.reserve(vertices.size() * 2u * sizeof(float));
			bone_indices.reserve(vertices.size() * 2u * sizeof(std::uint32_t));
			bone_weights.reserve(vertices.size() * 2u * sizeof(float));
			for (auto const& vertex : vertices) {
				for (auto const component : vertex.pos) {
					AppendFloat(positions, component);
				}
				for (auto const component : vertex.normal) {
					AppendFloat(normals, component);
				}
				for (auto const component : vertex.uv) {
					AppendFloat(texcoords, component);
				}
				AppendUInt32(bone_indices, vertex.bone[0]);
				AppendUInt32(bone_indices, vertex.bone[1]);
				auto const first_weight = static_cast<float>(vertex.weight) / 100.0f;
				AppendFloat(bone_weights, first_weight);
				AppendFloat(bone_weights, 1.0f - first_weight);
			}

			Binary indices;
			indices.reserve(source_indices.size() * sizeof(std::uint32_t));
			for (auto const index : source_indices) {
				if (index >= vertices.size()) {
					throw std::invalid_argument{"PMD source contains an invalid vertex index"};
				}
				AppendUInt32(indices, index);
			}

			Binary geometry;
			auto positions_range = AppendGeometryStream(geometry, std::move(positions));
			auto normals_range = AppendGeometryStream(geometry, std::move(normals));
			auto texcoords_range = AppendGeometryStream(geometry, std::move(texcoords));
			auto bone_indices_range = AppendGeometryStream(geometry, std::move(bone_indices));
			auto bone_weights_range = AppendGeometryStream(geometry, std::move(bone_weights));
			auto indices_range = AppendGeometryStream(geometry, std::move(indices));

			Array textures;
			std::vector<std::string> texture_paths;
			Array materials;
			Array meshes;
			std::uint64_t first_index = 0u;
			for (
			    std::size_t material_index = 0u; material_index < source_materials.size();
			    ++material_index
			) {
				auto const& material = source_materials[material_index];
				if (material.vertex_count % 3u != 0u ||
				    material.vertex_count > source_indices.size() - first_index) {
					throw std::invalid_argument{"PMD material contains an invalid index range"};
				}
				Array bindings;
				auto texture_name = PMDString(material.texture_filename, 20u);
				if (auto const separator = texture_name.find('*'); separator != std::string::npos) {
					texture_name.erase(separator);
				}
				if (!texture_name.empty()) {
					auto resolved = ResolveImportPath(
					    file_system,
					    ToVirtualPath(FromVirtualPath(entry).parent_path()),
					    texture_name
					);
					auto found = std::find(texture_paths.begin(), texture_paths.end(), resolved);
					auto texture_index = static_cast<std::size_t>(found - texture_paths.begin());
					if (found == texture_paths.end()) {
						texture_paths.emplace_back(resolved);
						textures.emplace_back(ImportTexture(
						    VirtualFilename(resolved),
						    file_system.Read(resolved),
						    true
						));
					}
					bindings.emplace_back(
					    Object{
					        {"semantic", Value{static_cast<std::uint64_t>(aiTextureType_DIFFUSE)}},
					        {"texture", Value{static_cast<std::uint64_t>(texture_index)}}
					    }
					);
				}
				materials.emplace_back(
				    Object{
				        {"diffuse", FloatVector(material.diffuse, 3u)},
				        {"alpha", Value{static_cast<double>(material.alpha)}},
				        {"specularity", Value{static_cast<double>(material.specularity)}},
				        {"specular", FloatVector(material.specular, 3u)},
				        {"ambient", FloatVector(material.ambient, 3u)},
				        {"textures", Value{std::move(bindings)}}
				    }
				);
				auto index_range = indices_range;
				index_range["offset"] =
				    Value{indices_range.at("offset").As<std::uint64_t>() + first_index * 4u};
				index_range["size"] = Value{static_cast<std::uint64_t>(material.vertex_count) * 4u};
				meshes.emplace_back(
				    Object{
				        {"material", Value{static_cast<std::uint64_t>(material_index)}},
				        {"vertex_count", Value{static_cast<std::uint64_t>(vertices.size())}},
				        {"index_count", Value{static_cast<std::uint64_t>(material.vertex_count)}},
				        {"positions", Value{positions_range}},
				        {"normals", Value{normals_range}},
				        {"texcoords", Value{texcoords_range}},
				        {"bone_indices", Value{bone_indices_range}},
				        {"bone_weights", Value{bone_weights_range}},
				        {"indices", Value{std::move(index_range)}}
				    }
				);
				first_index += material.vertex_count;
			}
			if (first_index != source_indices.size()) {
				throw std::invalid_argument{"PMD materials do not cover the complete index buffer"};
			}

			return Object{
			    {"geometry", Value{File{.m_name = "geometry.mesh", .m_data = std::move(geometry)}}},
			    {"meshes", Value{std::move(meshes)}},
			    {"materials", Value{std::move(materials)}},
			    {"textures", Value{std::move(textures)}},
			    {"animation_count", Value{std::uint64_t{0u}}}
			};
		}

		/**
		 * @brief Converts the geometry required to retain one imported mesh.
		 * @param mesh Assimp mesh to convert.
		 * @param geometry Shared model geometry storage.
		 * @param flip_texture_v Whether to undo an importer-specific UV vertical flip.
		 * @return Dynamic mesh description referencing @p geometry.
		 */
		[[nodiscard]] Object ConvertMesh(
		    aiMesh const& mesh,
		    Binary& geometry,
		    bool flip_texture_v
		) {
			Object result{
			    {"name", Value{std::string{mesh.mName.C_Str(), mesh.mName.length}}},
			    {"material", Value{static_cast<std::uint64_t>(mesh.mMaterialIndex)}},
			    {"vertex_count", Value{static_cast<std::uint64_t>(mesh.mNumVertices)}},
			    {"positions",
				 Value{
			         AppendGeometryStream(geometry, PackVectors(mesh.mVertices, mesh.mNumVertices))
			     }}
			};
			if (mesh.HasNormals()) {
				result.emplace(
				    "normals",
				    Value{AppendGeometryStream(
				        geometry,
				        PackVectors(mesh.mNormals, mesh.mNumVertices)
				    )}
				);
			}
			if (mesh.HasTextureCoords(0u)) {
				result.emplace(
				    "texcoords",
				    Value{AppendGeometryStream(
				        geometry,
				        PackTextureCoordinates(
				            mesh.mTextureCoords[0u],
				            mesh.mNumVertices,
				            flip_texture_v
				        )
				    )}
				);
			}
			if (mesh.HasTangentsAndBitangents()) {
				result.emplace(
				    "tangents",
				    Value{AppendGeometryStream(
				        geometry,
				        PackVectors(mesh.mTangents, mesh.mNumVertices)
				    )}
				);
				result.emplace(
				    "bitangents",
				    Value{AppendGeometryStream(
				        geometry,
				        PackVectors(mesh.mBitangents, mesh.mNumVertices)
				    )}
				);
			}

			Binary indices;
			for (std::size_t face_index = 0; face_index < mesh.mNumFaces; ++face_index) {
				auto const& face = mesh.mFaces[face_index];
				for (std::size_t index = 0; index < face.mNumIndices; ++index) {
					AppendUInt32(indices, face.mIndices[index]);
				}
			}
			result.emplace("indices", Value{AppendGeometryStream(geometry, std::move(indices))});
			result.emplace("index_count", Value{static_cast<std::uint64_t>(mesh.mNumFaces) * 3u});
			return result;
		}

		/** @brief Resolves one material texture path relative to the model entry. */
		[[nodiscard]] std::string ResolveDependencyPath(
		    ImportFileSystem const& file_system,
		    std::string_view entry,
		    std::string_view dependency
		) {
			return ResolveImportPath(
			    file_system,
			    ToVirtualPath(FromVirtualPath(entry).parent_path()),
			    dependency
			);
		}

		/** @brief Imports an Assimp-supported model through the private VFS adapter. */
		[[nodiscard]] Object ImportModel(
		    ImportFileSystem const& file_system,
		    std::string_view entry
		) {
			Assimp::Importer importer;
			importer.SetIOHandler(new AssimpIOSystem{file_system, entry});
			auto const entry_name = std::string{entry};
			auto const* scene = importer.ReadFile(
			    entry_name,
			    aiProcess_Triangulate | aiProcess_JoinIdenticalVertices
			);
			if (!scene) {
				throw std::runtime_error{importer.GetErrorString()};
			}

			Array meshes;
			Binary geometry;
			auto const flip_texture_v = LowercaseExtension(entry) == ".pmx";
			meshes.reserve(scene->mNumMeshes);
			for (std::size_t index = 0; index < scene->mNumMeshes; ++index) {
				meshes.emplace_back(ConvertMesh(*scene->mMeshes[index], geometry, flip_texture_v));
			}

			Array textures;
			std::vector<std::pair<std::string, aiTextureType>> texture_paths;
			Array materials;
			materials.reserve(scene->mNumMaterials);
			for (
			    std::size_t material_index = 0u; material_index < scene->mNumMaterials;
			    ++material_index
			) {
				auto const& material = *scene->mMaterials[material_index];
				aiString material_name;
				material.Get(AI_MATKEY_NAME, material_name);
				Array bindings;
				for (
				    int raw_type = aiTextureType_NONE + 1; raw_type <= AI_TEXTURE_TYPE_MAX;
				    ++raw_type
				) {
					auto const texture_type = static_cast<aiTextureType>(raw_type);
					for (
					    unsigned int index = 0u; index < material.GetTextureCount(texture_type);
					    ++index
					) {
						aiString texture_path;
						if (material.GetTexture(texture_type, index, &texture_path) != AI_SUCCESS ||
						    texture_path.length == 0u) {
							continue;
						}
						auto const srgb = IsSRGBTexture(texture_type);
						auto const normal = IsNormalTexture(texture_type);
						auto const source_path =
						    std::string_view{texture_path.C_Str(), texture_path.length};
						auto const* embedded = source_path.front() == '*' ?
						    scene->GetEmbeddedTexture(texture_path.C_Str()) :
						    nullptr;
						auto resolved = embedded ?
						    std::string{source_path} :
						    ResolveDependencyPath(file_system, entry, source_path);
						auto found = std::find(
						    texture_paths.begin(),
						    texture_paths.end(),
						    std::pair{resolved, texture_type}
						);
						std::size_t texture_index = 0u;
						if (found == texture_paths.end()) {
							texture_index = texture_paths.size();
							texture_paths.emplace_back(resolved, texture_type);
							if (embedded) {
								textures.emplace_back(
								    ImportTexture(*embedded, source_path, srgb, normal)
								);
							} else {
								textures.emplace_back(ImportTexture(
								    VirtualFilename(resolved),
								    file_system.Read(resolved),
								    srgb,
								    normal
								));
							}
						} else {
							texture_index = static_cast<std::size_t>(found - texture_paths.begin());
						}
						bindings.emplace_back(
						    Object{
						        {"semantic", Value{static_cast<std::uint64_t>(raw_type)}},
						        {"texture", Value{static_cast<std::uint64_t>(texture_index)}}
						    }
						);
					}
				}
				materials.emplace_back(
				    Object{
				        {"name", Value{std::string{material_name.C_Str(), material_name.length}}},
				        {"textures", Value{std::move(bindings)}}
				    }
				);
			}
			return Object{
			    {"geometry", Value{File{.m_name = "geometry.mesh", .m_data = std::move(geometry)}}},
			    {"meshes", Value{std::move(meshes)}},
			    {"materials", Value{std::move(materials)}},
			    {"textures", Value{std::move(textures)}},
			    {"animation_count", Value{static_cast<std::uint64_t>(scene->mNumAnimations)}}
			};
		}

		/**
		 * @brief Converts one JSON value into the owning dynamic value representation.
		 * @param source Parsed JSON value.
		 * @param depth Current recursive nesting depth.
		 * @return Equivalent owning dynamic value.
		 */
		[[nodiscard]] Value ConvertJSON(nlohmann::json const& source, std::size_t depth) {
			if (depth > s_maximum_nesting) {
				throw std::length_error{"Imported JSON exceeds the maximum nesting depth"};
			}

			if (source.is_null()) {
				return {};
			}
			if (source.is_boolean()) {
				return Value{source.get<bool>()};
			}
			if (source.is_number_unsigned()) {
				return Value{source.get<std::uint64_t>()};
			}
			if (source.is_number_integer()) {
				return Value{source.get<std::int64_t>()};
			}
			if (source.is_number_float()) {
				return Value{source.get<double>()};
			}
			if (source.is_string()) {
				return Value{source.get<std::string>()};
			}
			if (source.is_binary()) {
				auto const& bytes = source.get_binary();
				Binary result;
				result.reserve(bytes.size());
				for (auto byte : bytes) {
					result.push_back(static_cast<std::byte>(byte));
				}
				return Value{std::move(result)};
			}
			if (source.is_array()) {
				Array result;
				result.reserve(source.size());
				for (auto const& element : source) {
					result.emplace_back(ConvertJSON(element, depth + 1u));
				}
				return Value{std::move(result)};
			}
			if (source.is_object()) {
				Object result;
				for (auto const& [name, element] : source.items()) {
					result.emplace(name, ConvertJSON(element, depth + 1u));
				}
				return Value{std::move(result)};
			}

			throw std::invalid_argument{"Imported JSON contains an unsupported value"};
		}

		/**
		 * @brief Adds caller-provided persistent fields without overwriting parsed data.
		 * @param imported Fields produced by the selected parser.
		 * @param fields Additional persistent fields supplied by the caller.
		 * @return Combined asset fields.
		 */
		[[nodiscard]] Object MergeFields(Object&& imported, Object const& fields) {
			for (auto const& [name, value] : fields) {
				if (!imported.emplace(name, value).second) {
					throw std::invalid_argument{"An import field conflicts with parsed asset data"};
				}
			}
			return std::move(imported);
		}

		/**
		 * @brief Parses a JSON asset whose root must be an object.
		 * @param bytes Complete JSON file contents.
		 * @return Parsed dynamic object.
		 */
		[[nodiscard]] Object ImportJSON(std::vector<std::byte> const& bytes) {
			auto const* first = reinterpret_cast<char const*>(bytes.data());
			auto document = nlohmann::json::parse(first, first + bytes.size());
			if (!document.is_object()) {
				throw std::invalid_argument{"Imported JSON root must be an object"};
			}

			Object result;
			for (auto const& [name, element] : document.items()) {
				result.emplace(name, ConvertJSON(element, 1u));
			}
			return result;
		}
	} // namespace

	/**
	 * @brief Finds every supported model entry when a package has no manifest.
	 * @param file_system Package-backed virtual filesystem.
	 * @return Ordered import-relative model paths, or the only JSON source path.
	 * @throws std::invalid_argument if no supported entry exists or multiple JSON
	 * entries make a non-model package ambiguous.
	 */
	[[nodiscard]] std::vector<std::string> FindImportEntries(ImportFileSystem const& file_system) {
		std::vector<std::string> model_entries;
		std::vector<std::string> json_entries;
		Assimp::Importer model_probe;
		for (auto& path : file_system.ListFiles()) {
			auto const extension = LowercaseExtension(path);
			if (extension == ".pmd" || model_probe.IsExtensionSupported(extension)) {
				model_entries.emplace_back(std::move(path));
			} else if (
			    extension == ".json" && std::filesystem::path{path}.filename() != "asset.json"
			) {
				json_entries.emplace_back(std::move(path));
			}
		}

		if (!model_entries.empty()) {
			std::ranges::sort(model_entries);
			return model_entries;
		}
		if (json_entries.size() == 1u) {
			return {std::move(json_entries.front())};
		}
		if (json_entries.size() > 1u) {
			throw std::invalid_argument{"Import package contains multiple JSON entry files"};
		}
		throw std::invalid_argument{"Import package contains no supported entry file"};
	}

	/**
	 * @brief Imports one virtual source file into persistent dynamic fields.
	 *
	 * Recognized formats are parsed into named fields. Files without a registered
	 * internal parser are retained losslessly in the binary field.
	 *
	 * @param file_system Import-scoped directory or archive filesystem.
	 * @param entry Import-relative source file selected as the asset entry point.
	 * @param fields Additional persistent fields supplied by the caller.
	 * @return Complete fields stored by the imported asset.
	 * @throws std::exception if a recognized source is malformed or fields conflict.
	 */
	[[nodiscard]] Object ImportAsset(
	    ImportFileSystem const& file_system,
	    std::string_view entry,
	    Object const& fields
	) {
		auto bytes = file_system.Read(entry);
		auto const format = DetectSourceFormat(bytes);
		auto const extension = LowercaseExtension(entry);
		Assimp::Importer model_probe;
		if (format == SourceFormat::JSON && extension != ".gltf") {
			return MergeFields(ImportJSON(bytes), fields);
		}
		if (format == SourceFormat::PMD || extension == ".pmd") {
			if (model_probe.IsExtensionSupported(".pmd")) {
				return MergeFields(ImportModel(file_system, entry), fields);
			}
			return MergeFields(ImportPMD(file_system, entry, bytes), fields);
		}
		if (format == SourceFormat::GLB || format == SourceFormat::FBX) {
			return MergeFields(ImportModel(file_system, entry), fields);
		}
		if (format != SourceFormat::Unknown && format != SourceFormat::JSON) {
			Object imported;
			imported.emplace("binary", Value{std::move(bytes)});
			return MergeFields(std::move(imported), fields);
		}

		if (model_probe.IsExtensionSupported(extension)) {
			return MergeFields(ImportModel(file_system, entry), fields);
		}

		Object imported;
		imported.emplace("binary", Value{std::move(bytes)});
		return MergeFields(std::move(imported), fields);
	}

} // namespace fyuu_asset
