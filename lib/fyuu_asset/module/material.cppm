module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#endif

export module fyuu_asset:material;

#if defined(__cpp_lib_modules)
import std;
#endif
import :uuid;

export namespace fyuu_asset {

	struct Material {
		UUID pipeline;
		std::unordered_map<std::string, std::vector<float>> parameters;
		std::unordered_map<std::string, UUID> bitmaps;

		[[nodiscard]] bool Valid() const noexcept;
		void Serialize(std::filesystem::path const& path) const;
		[[nodiscard]] static Material Deserialize(std::filesystem::path const& path);
	};

}
