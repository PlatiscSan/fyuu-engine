module;
#include <fstream>
#include <nlohmann/json.hpp>

module fyuu_asset:scene_impl;

#if defined(__cpp_lib_modules)
import std;
#endif
import :scene;

namespace fyuu_asset {

	Scene::Entity& Scene::CreateEntity(std::string const& name) {
		if (name.empty()) {
			throw std::invalid_argument("Scene entity name cannot be empty");
		}
		entities.push_back(Entity{
			.id = UUID::Generate(),
			.name = name
		});
		return entities.back();
	}

	bool Scene::Valid() const {
		std::unordered_map<UUID, Entity const*> indexed;
		indexed.reserve(entities.size());
		for (auto const& entity : entities) {
			if (entity.id.IsNil() || entity.name.empty() || !indexed.emplace(entity.id, &entity).second) {
				return false;
			}
		}
		for (auto const& entity : entities) {
			if (!entity.parent.IsNil() && !indexed.contains(entity.parent)) {
				return false;
			}
			auto current = &entity;
			for (std::size_t depth = 0; !current->parent.IsNil(); ++depth) {
				if (depth >= entities.size()) {
					return false;
				}
				current = indexed.at(current->parent);
			}
		}
		return true;
	}

	void Scene::Serialize(std::filesystem::path const& path) const {
		if (!Valid()) {
			throw std::runtime_error("Cannot serialize invalid scene");
		}
		nlohmann::json document = nlohmann::json::array();
		for (auto const& entity : entities) {
			nlohmann::json serialized{
				{ "id", entity.id.ToString() },
				{ "parent", entity.parent.IsNil() ? nlohmann::json(nullptr) : nlohmann::json(entity.parent.ToString()) },
				{ "name", entity.name },
				{ "translation", entity.translation },
				{ "rotation", entity.rotation },
				{ "scale", entity.scale },
				{ "mesh", entity.mesh.IsNil() ? nlohmann::json(nullptr) : nlohmann::json(entity.mesh.ToString()) },
				{ "material", entity.material.IsNil() ? nlohmann::json(nullptr) : nlohmann::json(entity.material.ToString()) }
			};
			document.push_back(std::move(serialized));
		}
		auto temporary = path;
		temporary += ".tmp";
		{
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			output << document.dump(2);
			if (!output) {
				throw std::runtime_error("Failed to write scene file");
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
			throw std::runtime_error("Failed to publish scene file");
		}
	}

	Scene Scene::Deserialize(std::filesystem::path const& path) {
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			throw std::runtime_error("Failed to open scene file");
		}
		auto document = nlohmann::json::parse(input);
		Scene scene;
		scene.entities.reserve(document.size());
		for (auto const& source : document) {
			Entity entity{
				.id = UUID::Parse(source.at("id").get<std::string>()),
				.parent = {},
				.name = source.at("name").get<std::string>(),
				.translation = source.at("translation").get<std::array<float, 3>>(),
				.rotation = source.at("rotation").get<std::array<float, 4>>(),
				.scale = source.at("scale").get<std::array<float, 3>>(),
				.mesh = {},
				.material = {}
			};
			if (!source.at("parent").is_null()) {
				entity.parent = UUID::Parse(source.at("parent").get<std::string>());
			}
			if (!source.at("mesh").is_null()) {
				entity.mesh = UUID::Parse(source.at("mesh").get<std::string>());
			}
			if (!source.at("material").is_null()) {
				entity.material = UUID::Parse(source.at("material").get<std::string>());
			}
			scene.entities.push_back(std::move(entity));
		}
		if (!scene.Valid()) {
			throw std::runtime_error("Loaded scene is invalid");
		}
		return scene;
	}

}
