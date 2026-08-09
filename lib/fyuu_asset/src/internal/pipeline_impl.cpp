module;
#include <fstream>
#include <nlohmann/json.hpp>

module fyuu_asset:pipeline_impl;

#if defined(__cpp_lib_modules)
import std;
#endif
import :pipeline;

namespace fyuu_asset {

	bool Pipeline::Valid() const noexcept {
		if (stages.empty()) {
			return false;
		}
		std::uint32_t seen = 0;
		for (auto const& entry : stages) {
			if (entry.shader.IsNil() || entry.entry_point.empty()) {
				return false;
			}
			auto bit = std::uint32_t{ 1 } << static_cast<std::uint8_t>(entry.stage);
			if ((seen & bit) != 0) {
				return false;
			}
			seen |= bit;
		}
		auto compute = std::uint32_t{ 1 } << static_cast<std::uint8_t>(ShaderStage::Compute);
		if (type == PipelineType::Compute) {
			return stages.size() == 1 && (seen & compute) != 0;
		}
		return (seen & compute) == 0;
	}

	void Pipeline::Serialize(std::filesystem::path const& path) const {
		if (!Valid()) {
			throw std::runtime_error("Cannot serialize an invalid pipeline");
		}
		nlohmann::json document{
			{ "type", static_cast<std::uint8_t>(type) },
			{ "stages", nlohmann::json::array() }
		};
		for (auto const& entry : stages) {
			document["stages"].push_back({
				{ "shader", entry.shader.ToString() },
				{ "entry_point", entry.entry_point },
				{ "stage", static_cast<std::uint8_t>(entry.stage) }
			});
		}
		auto temporary = path;
		temporary += ".tmp";
		{
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			if (!output) {
				throw std::runtime_error("Failed to open pipeline file");
			}
			output << document.dump(2);
			if (!output) {
				throw std::runtime_error("Failed to write pipeline file");
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
			throw std::runtime_error("Failed to publish pipeline file");
		}
	}

	Pipeline Pipeline::Deserialize(std::filesystem::path const& path) {
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			throw std::runtime_error("Failed to open pipeline file");
		}
		auto document = nlohmann::json::parse(input);
		Pipeline pipeline{
			.type = static_cast<PipelineType>(document.at("type").get<std::uint8_t>()),
			.stages = {}
		};
		for (auto const& source : document.at("stages")) {
			pipeline.stages.emplace_back(
				UUID::Parse(source.at("shader").get<std::string>()),
				source.at("entry_point").get<std::string>(),
				static_cast<ShaderStage>(source.at("stage").get<std::uint8_t>())
			);
		}
		if (!pipeline.Valid()) {
			throw std::runtime_error("Loaded pipeline is invalid");
		}
		return pipeline;
	}

}
