module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#endif
#include <boost/uuid.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <nlohmann/json.hpp>

export module fyuu_asset:pipeline;

#if defined(__cpp_lib_modules)
import std;
#endif

export namespace fyuu_asset {

	enum class PipelineType : std::uint8_t {
		Graphics,
		Compute
	};

	enum class ShaderStage : std::uint8_t {
		Vertex,
		Fragment,
		TessellationControl,
		TessellationEvaluation,
		Geometry,
		Compute,
		Task,
		Mesh
	};

	struct Pipeline {
		struct Stage {
			boost::uuids::uuid shader;
			std::string entry_point;
			ShaderStage stage = ShaderStage::Vertex;
		};

		PipelineType type = PipelineType::Graphics;
		std::vector<Stage> stages;

		[[nodiscard]] bool Valid() const noexcept {
			if (stages.empty()) {
				return false;
			}

			std::uint32_t seen = 0;
			for (auto const& entry : stages) {
				if (entry.shader.is_nil() || entry.entry_point.empty()) {
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

		void Serialize(std::filesystem::path const& path) const {
			if (!Valid()) {
				throw std::runtime_error("Cannot serialize an invalid pipeline");
			}

			nlohmann::json document{
				{ "type", static_cast<std::uint8_t>(type) },
				{ "stages", nlohmann::json::array() }
			};
			for (auto const& entry : stages) {
				document["stages"].push_back({
					{ "shader", boost::uuids::to_string(entry.shader) },
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

		[[nodiscard]] static Pipeline Deserialize(std::filesystem::path const& path) {
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
					boost::uuids::string_generator{}(source.at("shader").get<std::string>()),
					source.at("entry_point").get<std::string>(),
					static_cast<ShaderStage>(source.at("stage").get<std::uint8_t>())
				);
			}
			if (!pipeline.Valid()) {
				throw std::runtime_error("Loaded pipeline is invalid");
			}
			return pipeline;
		}
	};

}
