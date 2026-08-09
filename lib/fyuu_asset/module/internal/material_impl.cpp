module;
#include <fstream>
#include <nlohmann/json.hpp>

module fyuu_asset:material_impl;

#if defined(__cpp_lib_modules)
import std;
#endif
import :material;

namespace fyuu_asset {

	bool Material::Valid() const noexcept {
		if (pipeline.IsNil()) {
			return false;
		}
		for (auto const& [name, value] : parameters) {
			if (name.empty() || value.empty()) {
				return false;
			}
		}
		for (auto const& [name, bitmap] : bitmaps) {
			if (name.empty() || bitmap.IsNil()) {
				return false;
			}
		}
		return true;
	}

	void Material::Serialize(std::filesystem::path const& path) const {
		if (!Valid()) {
			throw std::runtime_error("Cannot serialize invalid material");
		}
		nlohmann::json document{
			{ "pipeline", pipeline.ToString() },
			{ "parameters", parameters },
			{ "bitmaps", nlohmann::json::object() }
		};
		for (auto const& [name, bitmap] : bitmaps) {
			document["bitmaps"][name] = bitmap.ToString();
		}
		auto temporary = path;
		temporary += ".tmp";
		{
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			output << document.dump(2);
			if (!output) {
				throw std::runtime_error("Failed to write material file");
			}
		}
		std::error_code error;
		std::filesystem::rename(temporary, path, error);
		if (error) {
			std::filesystem::remove(path, error);
			error.clear();
			std::filesystem::rename(temporary, path, error);
		}
		if (error) {
			throw std::runtime_error("Failed to publish material file");
		}
	}

	Material Material::Deserialize(std::filesystem::path const& path) {
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			throw std::runtime_error("Failed to open material file");
		}
		auto document = nlohmann::json::parse(input);
		Material material{
			.pipeline = UUID::Parse(document.at("pipeline").get<std::string>()),
			.parameters = document.at("parameters").get<std::unordered_map<std::string, std::vector<float>>>(),
			.bitmaps = {}
		};
		for (auto const& [name, value] : document.at("bitmaps").items()) {
			material.bitmaps.emplace(name, UUID::Parse(value.get<std::string>()));
		}
		if (!material.Valid()) {
			throw std::runtime_error("Loaded material is invalid");
		}
		return material;
	}

}
