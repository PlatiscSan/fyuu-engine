module;
#include <fstream>
#include <nlohmann/json.hpp>

module fyuu_asset:shader_impl;

import :shader;

namespace fyuu_asset {

	bool Shader::Valid() const noexcept {
		return !name.empty() && !source.empty();
	}

	void Shader::Serialize(std::filesystem::path const& path) const {
		if (!Valid()) {
			throw std::runtime_error("Cannot serialize invalid shader");
		}
		nlohmann::json document{
			{ "name", name },
			{ "source", source }
		};
		auto temporary = path;
		temporary += ".tmp";
		{
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			output << document.dump(2);
			if (!output) {
				throw std::runtime_error("Failed to write shader file");
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
			throw std::runtime_error("Failed to publish shader file");
		}
	}

	Shader Shader::Deserialize(std::filesystem::path const& path) {
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			throw std::runtime_error("Failed to open shader file");
		}
		auto document = nlohmann::json::parse(input);
		Shader shader{
			.name = document.at("name").get<std::string>(),
			.source = document.at("source").get<std::string>()
		};
		if (!shader.Valid()) {
			throw std::runtime_error("Loaded shader is invalid");
		}
		return shader;
	}

}
