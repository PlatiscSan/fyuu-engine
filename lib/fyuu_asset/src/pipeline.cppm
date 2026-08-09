module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#endif

export module fyuu_asset:pipeline;

#if defined(__cpp_lib_modules)
import std;
#endif
import :uuid;

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
			UUID shader;
			std::string entry_point;
			ShaderStage stage = ShaderStage::Vertex;
		};

		PipelineType type = PipelineType::Graphics;
		std::vector<Stage> stages;

		[[nodiscard]] bool Valid() const noexcept;
		void Serialize(std::filesystem::path const& path) const;
		[[nodiscard]] static Pipeline Deserialize(std::filesystem::path const& path);
	};

}
