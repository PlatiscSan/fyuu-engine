module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#endif
#include <boost/uuid.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <nlohmann/json.hpp>

export module fyuu_asset:material;

#if defined(__cpp_lib_modules)
import std;
#endif

export namespace fyuu_asset {

	struct Material {
		boost::uuids::uuid pipeline;
		std::unordered_map<std::string, std::vector<float>> parameters;
		std::unordered_map<std::string, boost::uuids::uuid> bitmaps;

		[[nodiscard]] bool Valid() const noexcept {
			if (pipeline.is_nil()) {
				return false;
			}
			for (auto const& [name, value] : parameters) {
				if (name.empty() || value.empty()) {
					return false;
				}
			}
			for (auto const& [name, bitmap] : bitmaps) {
				if (name.empty() || bitmap.is_nil()) {
					return false;
				}
			}
			return true;
		}

		void Serialize(std::filesystem::path const& path) const {
			if (!Valid()) {
				throw std::runtime_error("Cannot serialize invalid material");
			}
			nlohmann::json document{
				{ "pipeline", boost::uuids::to_string(pipeline) },
				{ "parameters", parameters },
				{ "bitmaps", nlohmann::json::object() }
			};
			for (auto const& [name, bitmap] : bitmaps) {
				document["bitmaps"][name] = boost::uuids::to_string(bitmap);
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

		[[nodiscard]] static Material Deserialize(std::filesystem::path const& path) {
			std::ifstream input(path, std::ios::binary);
			if (!input) {
				throw std::runtime_error("Failed to open material file");
			}
			auto document = nlohmann::json::parse(input);
			Material material{
				.pipeline = boost::uuids::string_generator{}(document.at("pipeline").get<std::string>()),
				.parameters = document.at("parameters").get<std::unordered_map<std::string, std::vector<float>>>(),
				.bitmaps = {}
			};
			for (auto const& [name, value] : document.at("bitmaps").items()) {
				material.bitmaps.emplace(name, boost::uuids::string_generator{}(value.get<std::string>()));
			}
			if (!material.Valid()) {
				throw std::runtime_error("Loaded material is invalid");
			}
			return material;
		}
	};

}
