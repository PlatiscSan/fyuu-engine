module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <utility>

#include <string>

#include <fstream>

#include <cstdint>

#include <string_view>

#include <filesystem>
#endif // !defined(__cpp_lib_modules)
#include <nlohmann/json.hpp>

module fyuu_asset:serialization;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :uuid;
import :value;
import :import_file_system;

namespace fyuu_asset {

	namespace {
		constexpr std::size_t s_maximum_nesting = 256u;
		constexpr std::string_view s_base64_alphabet =
		    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		/** @brief Encodes binary data without expanding each byte into a JSON node. */
		[[nodiscard]] std::string EncodeBase64(Binary const& binary) {
			std::string result;
			result.reserve(((binary.size() + 2u) / 3u) * 4u);
			for (std::size_t offset = 0u; offset < binary.size(); offset += 3u) {
				auto const first = std::to_integer<std::uint32_t>(binary[offset]);
				auto const second = offset + 1u < binary.size()
				    ? std::to_integer<std::uint32_t>(binary[offset + 1u])
				    : 0u;
				auto const third = offset + 2u < binary.size()
				    ? std::to_integer<std::uint32_t>(binary[offset + 2u])
				    : 0u;
				auto const packed = (first << 16u) | (second << 8u) | third;
				result.push_back(s_base64_alphabet[(packed >> 18u) & 0x3fu]);
				result.push_back(s_base64_alphabet[(packed >> 12u) & 0x3fu]);
				result.push_back(
				    offset + 1u < binary.size() ? s_base64_alphabet[(packed >> 6u) & 0x3fu] : '='
				);
				result.push_back(
				    offset + 2u < binary.size() ? s_base64_alphabet[packed & 0x3fu] : '='
				);
			}
			return result;
		}

		/** @brief Returns the six-bit value represented by one Base64 character. */
		[[nodiscard]] std::uint32_t DecodeBase64Character(char character) {
			auto const position = s_base64_alphabet.find(character);
			if (position == std::string_view::npos) {
				throw std::invalid_argument{"Asset binary field contains invalid Base64"};
			}
			return static_cast<std::uint32_t>(position);
		}

		/** @brief Decodes one canonical Base64 string into binary data. */
		[[nodiscard]] Binary DecodeBase64(std::string_view text) {
			if (text.size() % 4u != 0u) {
				throw std::invalid_argument{"Asset binary field has invalid Base64 length"};
			}
			Binary result;
			result.reserve((text.size() / 4u) * 3u);
			for (std::size_t offset = 0u; offset < text.size(); offset += 4u) {
				auto const final_group = offset + 4u == text.size();
				auto const padding = text[offset + 3u] == '=' ? (text[offset + 2u] == '=' ? 2u : 1u) : 0u;
				if ((!final_group && padding != 0u) || text[offset] == '=' || text[offset + 1u] == '=') {
					throw std::invalid_argument{"Asset binary field contains invalid Base64 padding"};
				}
				auto const first = DecodeBase64Character(text[offset]);
				auto const second = DecodeBase64Character(text[offset + 1u]);
				auto const third = padding == 2u ? 0u : DecodeBase64Character(text[offset + 2u]);
				auto const fourth = padding != 0u ? 0u : DecodeBase64Character(text[offset + 3u]);
				auto const packed = (first << 18u) | (second << 12u) | (third << 6u) | fourth;
				result.push_back(static_cast<std::byte>((packed >> 16u) & 0xffu));
				if (padding < 2u) {
					result.push_back(static_cast<std::byte>((packed >> 8u) & 0xffu));
				}
				if (padding == 0u) {
					result.push_back(static_cast<std::byte>(packed & 0xffu));
				}
			}
			return result;
		}

		/** @brief Replaces characters that cannot safely occur in an attached filename. */
		[[nodiscard]] std::string SanitizeFilename(std::string_view name) {
			auto result = ToVirtualPath(FromVirtualPath(name).filename());
			for (auto& character : result) {
				if (static_cast<unsigned char>(character) < 0x20u ||
				    std::string_view{"<>:\"/\\|?*"}.contains(character)) {
					character = '_';
				}
			}
			if (result.empty() || result == "." || result == "..") {
				result = "file";
			}
			return result;
		}

		/** @brief Selects the type-local directory for one recognized file extension. */
		[[nodiscard]] std::string_view AttachedDirectory(std::string_view name) {
			auto extension = ToVirtualPath(FromVirtualPath(name).extension());
			for (auto& character : extension) {
				if (character >= 'A' && character <= 'Z') {
					character = static_cast<char>(character - 'A' + 'a');
				}
			}
			if (extension == ".mesh") {
				return "mesh";
			}
			if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
			    extension == ".tga" || extension == ".bmp" || extension == ".gif" ||
			    extension == ".webp" || extension == ".dds" || extension == ".ktx" ||
			    extension == ".ktx2" || extension == ".hdr" || extension == ".exr" ||
			    extension == ".texture") {
				return "texture";
			}
			if (extension == ".anime" || extension == ".animation" || extension == ".vmd") {
				return "anime";
			}
			if (extension == ".skeleton") {
				return "skeleton";
			}
			if (extension == ".wav" || extension == ".ogg" || extension == ".mp3" ||
			    extension == ".flac") {
				return "audio";
			}
			if (extension == ".spv" || extension == ".dxil" || extension == ".metallib") {
				return "shader";
			}
			return "file";
		}

		/** @brief Writes one recognized file beside its containing asset JSON. */
		[[nodiscard]] std::string WriteAttachedFile(
		    std::filesystem::path const& asset_path,
		    File const& file,
		    std::size_t& file_index
		) {
			auto const filename = ToVirtualPath(asset_path.stem()) + "_" +
			    std::to_string(file_index++) + "_" + SanitizeFilename(file.m_name);
			auto const directory = std::filesystem::path{AttachedDirectory(file.m_name)};
			std::filesystem::create_directories(asset_path.parent_path() / directory);
		std::ofstream output{
			    asset_path.parent_path() / directory / FromVirtualPath(filename),
			    std::ios::binary | std::ios::trunc
			};
			if (!output || !output.write(
			                   reinterpret_cast<char const*>(file.m_data.data()),
			                   static_cast<std::streamsize>(file.m_data.size())
			               )) {
				throw std::runtime_error{"Failed to persist an attached asset file"};
			}
			return ToVirtualPath(directory / FromVirtualPath(filename));
		}

		/** @brief Reads one attached file confined to the asset JSON directory. */
		[[nodiscard]] File ReadAttachedFile(
		    std::filesystem::path const& asset_path,
		    std::string_view filename
		) {
			auto const relative = FromVirtualPath(filename);
			if (relative.is_absolute() || relative.has_root_name() ||
			    relative.has_root_directory() || relative.lexically_normal() != relative) {
				throw std::invalid_argument{"Asset file reference must be a relative path"};
			}
			auto component_count = std::size_t{0u};
			for (auto const& component : relative) {
				if (component == "." || component == "..") {
					throw std::invalid_argument{"Asset file reference escapes its asset directory"};
				}
				++component_count;
			}
			if (component_count != 2u) {
				throw std::invalid_argument{
				    "Asset file reference must contain one category directory"
				};
			}
			auto const path = asset_path.parent_path() / relative;
			auto const size = std::filesystem::file_size(path);
			if (size > Binary{}.max_size()) {
				throw std::length_error{"Attached asset file is too large"};
			}
			File result{
			    .m_name = ToVirtualPath(relative),
			    .m_data = Binary(static_cast<std::size_t>(size))
			};
			std::ifstream input{path, std::ios::binary};
			if (!input || !input.read(
			                  reinterpret_cast<char*>(result.m_data.data()),
			                  static_cast<std::streamsize>(result.m_data.size())
			              )) {
				throw std::runtime_error{"Failed to read an attached asset file"};
			}
			return result;
		}

		/** @brief Encodes one dynamic value with an explicit persistent type tag. */
		[[nodiscard]] nlohmann::json EncodeValue(
		    Value const& value,
		    std::size_t depth,
		    std::filesystem::path const& asset_path,
		    std::size_t& file_index
		) {
			if (depth > s_maximum_nesting) {
				throw std::length_error{"Asset value exceeds the maximum nesting depth"};
			}
			if (value.Is<std::monostate>()) {
				return {{"type", "null"}};
			}
			if (value.Is<bool>()) {
				return {{"type", "bool"}, {"value", value.As<bool>()}};
			}
			if (value.Is<std::int64_t>()) {
				return {{"type", "int64"}, {"value", value.As<std::int64_t>()}};
			}
			if (value.Is<std::uint64_t>()) {
				return {{"type", "uint64"}, {"value", value.As<std::uint64_t>()}};
			}
			if (value.Is<double>()) {
				return {{"type", "double"}, {"value", value.As<double>()}};
			}
			if (value.Is<std::string>()) {
				return {{"type", "string"}, {"value", value.As<std::string>()}};
			}
			if (value.Is<Binary>()) {
				return {{"type", "binary"}, {"value", EncodeBase64(value.As<Binary>())}};
			}
			if (value.Is<File>()) {
				auto const& file = value.As<File>();
				return {
				    {"type", "file"},
				    {"value",
				     {{"file", WriteAttachedFile(asset_path, file, file_index)},
				      {"name", file.m_name}}}
				};
			}
			if (value.Is<Array>()) {
				nlohmann::json encoded = nlohmann::json::array();
				for (auto const& element : value.As<Array>()) {
					encoded.push_back(EncodeValue(element, depth + 1u, asset_path, file_index));
				}
				return {{"type", "array"}, {"value", std::move(encoded)}};
			}
			if (value.Is<Object>()) {
				nlohmann::json encoded = nlohmann::json::object();
				for (auto const& [name, element] : value.As<Object>()) {
					encoded[name] = EncodeValue(element, depth + 1u, asset_path, file_index);
				}
				return {{"type", "object"}, {"value", std::move(encoded)}};
			}
			return {{"type", "uuid"}, {"value", FormatUUID(value.As<UUID>())}};
		}

		/** @brief Decodes one explicitly tagged persistent dynamic value. */
		[[nodiscard]] Value DecodeValue(
		    nlohmann::json const& encoded,
		    std::size_t depth,
		    std::filesystem::path const& asset_path
		) {
			if (depth > s_maximum_nesting || !encoded.is_object()) {
				throw std::invalid_argument{"Asset contains an invalid dynamic value"};
			}
			auto const& type = encoded.at("type").get_ref<std::string const&>();
			if (type == "null") {
				return {};
			}
			if (type == "bool") {
				return Value{encoded.at("value").get<bool>()};
			}
			if (type == "int64") {
				return Value{encoded.at("value").get<std::int64_t>()};
			}
			if (type == "uint64") {
				return Value{encoded.at("value").get<std::uint64_t>()};
			}
			if (type == "double") {
				return Value{encoded.at("value").get<double>()};
			}
			if (type == "string") {
				return Value{encoded.at("value").get<std::string>()};
			}
			if (type == "binary") {
				return Value{
				    DecodeBase64(encoded.at("value").get_ref<std::string const&>())
				};
			}
			if (type == "file") {
				auto file = ReadAttachedFile(
				    asset_path,
				    encoded.at("value").at("file").get_ref<std::string const&>()
				);
				file.m_name = encoded.at("value").at("name").get<std::string>();
				return Value{std::move(file)};
			}
			if (type == "uuid") {
				return Value{ParseUUID(encoded.at("value").get_ref<std::string const&>())};
			}
			if (type == "array") {
				Array result;
				for (auto const& element : encoded.at("value")) {
					result.emplace_back(DecodeValue(element, depth + 1u, asset_path));
				}
				return Value{std::move(result)};
			}
			if (type == "object") {
				Object result;
				for (auto const& [name, element] : encoded.at("value").items()) {
					result.emplace(name, DecodeValue(element, depth + 1u, asset_path));
				}
				return Value{std::move(result)};
			}
			throw std::invalid_argument{"Asset contains an unknown dynamic value type"};
		}
	} // namespace

	/** @brief Serializes dynamic asset fields to one persistent JSON document. */
	void SerializeAssetFields(std::filesystem::path const& path, Object const& fields) {
		nlohmann::json document = nlohmann::json::object();
		std::size_t file_index = 0u;
		for (auto const& [name, value] : fields) {
			document[name] = EncodeValue(value, 1u, path, file_index);
		}
		std::ofstream output{path, std::ios::binary | std::ios::trunc};
		if (!output) {
			throw std::runtime_error{"Failed to open asset for serialization"};
		}
		output << document.dump();
		if (!output) {
			throw std::runtime_error{"Failed to serialize asset"};
		}
	}

	/** @brief Deserializes dynamic asset fields from one persistent JSON document. */
	[[nodiscard]] Object DeserializeAssetFields(std::filesystem::path const& path) {
		std::ifstream input{path, std::ios::binary};
		if (!input) {
			throw std::runtime_error{"Failed to open asset for deserialization"};
		}
		auto const document = nlohmann::json::parse(input);
		if (!document.is_object()) {
			throw std::invalid_argument{"Serialized asset root must be an object"};
		}
		Object fields;
		for (auto const& [name, value] : document.items()) {
			fields.emplace(name, DecodeValue(value, 1u, path));
		}
		return fields;
	}

} // namespace fyuu_asset
