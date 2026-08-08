module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#endif
#include <nlohmann/json.hpp>

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

		Entity& CreateEntity(std::string const& name) {
			if (name.empty()) {
				throw std::invalid_argument("Scene entity name cannot be empty");
			}
			entities.push_back(Entity{
				.id = GenerateUUID(),
				.name = name
			});
			return entities.back();
		}

		[[nodiscard]] bool Valid() const {
			std::unordered_map<UUID, Entity const*, UUIDHash, UUIDEquality> indexed;
			indexed.reserve(entities.size());
			for (auto const& entity : entities) {
				if (entity.id.is_nil() || entity.name.empty() || !indexed.emplace(entity.id, &entity).second) {
					return false;
				}
			}

			for (auto const& entity : entities) {
				if (!entity.parent.is_nil() && !indexed.contains(entity.parent)) {
					return false;
				}

				auto current = &entity;
				for (std::size_t depth = 0; !current->parent.is_nil(); ++depth) {
					if (depth >= entities.size()) {
						return false;
					}
					current = indexed.at(current->parent);
				}
			}
			return true;
		}

		void Serialize(std::filesystem::path const& path) const {
			if (!Valid()) {
				throw std::runtime_error("Cannot serialize invalid scene");
			}

			nlohmann::json document = nlohmann::json::array();
			for (auto const& entity : entities) {
				nlohmann::json serialized{
					{ "id", UUIDToString(entity.id) },
					{ "parent", UUIDIsNil(entity.parent) ? nlohmann::json(nullptr) : nlohmann::json(UUIDToString(entity.parent)) },
					{ "name", entity.name },
					{ "translation", entity.translation },
					{ "rotation", entity.rotation },
					{ "scale", entity.scale },
					{ "mesh", UUIDIsNil(entity.mesh) ? nlohmann::json(nullptr) : nlohmann::json(UUIDToString(entity.mesh)) },
					{ "material", UUIDIsNil(entity.material) ? nlohmann::json(nullptr) : nlohmann::json(UUIDToString(entity.material)) }
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

		[[nodiscard]] static Scene Deserialize(std::filesystem::path const& path) {
			std::ifstream input(path, std::ios::binary);
			if (!input) {
				throw std::runtime_error("Failed to open scene file");
			}
			auto document = nlohmann::json::parse(input);
			Scene scene;
			scene.entities.reserve(document.size());
			for (auto const& source : document) {
				Entity entity{
					.id = ParseUUID(source.at("id").get<std::string>()),
					.parent = {},
					.name = source.at("name").get<std::string>(),
					.translation = source.at("translation").get<std::array<float, 3>>(),
					.rotation = source.at("rotation").get<std::array<float, 4>>(),
					.scale = source.at("scale").get<std::array<float, 3>>(),
					.mesh = {},
					.material = {}
				};
				if (!source.at("parent").is_null()) {
					entity.parent = ParseUUID(source.at("parent").get<std::string>());
				}
				if (!source.at("mesh").is_null()) {
					entity.mesh = ParseUUID(source.at("mesh").get<std::string>());
				}
				if (!source.at("material").is_null()) {
					entity.material = ParseUUID(source.at("material").get<std::string>());
				}
				scene.entities.push_back(std::move(entity));
			}
			if (!scene.Valid()) {
				throw std::runtime_error("Loaded scene is invalid");
			}
			return scene;
		}
	};

}
