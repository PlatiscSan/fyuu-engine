module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#endif

export module fyuu_asset:scene;

#if defined(__cpp_lib_modules)
import std;
#endif
import :uuid;

export namespace fyuu_asset {

	struct Scene {
		struct Entity {
			UUID id;
			UUID parent;
			std::string name;
			std::array<float, 3> translation{ 0.0f, 0.0f, 0.0f };
			std::array<float, 4> rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
			std::array<float, 3> scale{ 1.0f, 1.0f, 1.0f };
			UUID mesh;
			UUID material;
		};

		std::vector<Entity> entities;

		Entity& CreateEntity(std::string const& name);
		[[nodiscard]] bool Valid() const;
		void Serialize(std::filesystem::path const& path) const;
		[[nodiscard]] static Scene Deserialize(std::filesystem::path const& path);
	};

}
