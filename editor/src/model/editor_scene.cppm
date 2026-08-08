module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <algorithm>
#endif // !defined(__cpp_lib_modules)

module fyuu_editor:scene;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_engine;

namespace fyuu_editor {

	using EntityId = fyuu_asset::UUID;
	using Entity = fyuu_engine::Scene::Entity;

	[[nodiscard]] bool EntityEqual(Entity const& left, Entity const& right) noexcept {
		return fyuu_asset::UUIDEqual(left.id, right.id)
			&& fyuu_asset::UUIDEqual(left.parent, right.parent)
			&& left.name == right.name
			&& left.translation == right.translation
			&& left.rotation == right.rotation
			&& left.scale == right.scale
			&& fyuu_asset::UUIDEqual(left.mesh, right.mesh)
			&& fyuu_asset::UUIDEqual(left.material, right.material);
	}

	[[nodiscard]] bool SceneEqual(
		fyuu_engine::Scene const& left,
		fyuu_engine::Scene const& right
	) noexcept {
		return std::ranges::equal(left.entities, right.entities, EntityEqual);
	}

class EditorScene {
public:
	enum class SaveResult {
		None,
		Succeeded,
		Failed
	};

	struct Snapshot {
		fyuu_engine::Scene scene;
		bool dirty = false;

		[[nodiscard]] bool operator==(Snapshot const& other) const noexcept {
			return dirty == other.dirty && SceneEqual(scene, other.scene);
		}
	};

		EditorScene()
			: m_asset(fyuu_engine::SceneAsset::Create(fyuu_engine::Scene{})) {
		}

		~EditorScene() noexcept {
			if (m_save) {
				try {
					m_save->Wait();
				}
				catch (...) {
				}
			}
		}

		EntityId CreateEntity(std::string const& name) {
			auto& entity = m_asset->Get().CreateEntity(name);
			MarkDirty();
			return entity.id;
		}

		bool DestroyEntity(EntityId const& id) {
			auto const removed = std::erase_if(m_asset->Get().entities, [&id](Entity const& entity) {
				return fyuu_asset::UUIDEqual(entity.id, id);
			});
			if (removed != 0u) {
				MarkDirty();
			}
			return removed != 0u;
		}

		[[nodiscard]] Entity* FindEntity(EntityId const& id) {
			auto& entities = m_asset->Get().entities;
			auto iterator = std::ranges::find_if(entities, [&id](Entity const& entity) {
				return fyuu_asset::UUIDEqual(entity.id, id);
			});
			return iterator == entities.end() ? nullptr : &*iterator;
		}

		[[nodiscard]] Entity const* FindEntity(EntityId const& id) const {
			auto const& entities = m_asset->Get().entities;
			auto iterator = std::ranges::find_if(
				entities,
				[&id](Entity const& entity) {
					return fyuu_asset::UUIDEqual(entity.id, id);
				}
			);
			return iterator == entities.end() ? nullptr : &*iterator;
		}

		[[nodiscard]] std::vector<Entity> const& Entities() const noexcept {
			return m_asset->Get().entities;
		}

		[[nodiscard]] bool Dirty() const noexcept {
			return m_dirty;
		}

		void MarkDirty() noexcept {
			m_dirty = true;
			m_changed_during_save = m_changed_during_save || m_save.has_value();
		}

    void MarkSaved() noexcept {
        m_dirty = false;
    }

	[[nodiscard]] bool Saving() const noexcept {
		return m_save.has_value();
	}

	bool BeginSave() {
		if (m_save || !m_asset->Get().Valid()) {
			return false;
		}
		m_save_error.clear();
		m_changed_during_save = false;
		m_save.emplace(fyuu_engine::SaveScene(m_asset));
		return true;
	}

	SaveResult UpdateSave() noexcept {
		if (!m_save || !m_save->IsDone()) {
			return SaveResult::None;
		}
		try {
			m_save->Wait();
			if (!m_changed_during_save) {
				m_dirty = false;
			}
			m_save.reset();
			return SaveResult::Succeeded;
		}
		catch (std::exception const& exception) {
			try {
				m_save_error = exception.what();
			}
			catch (...) {
			}
		}
		catch (...) {
			try {
				m_save_error = "Unknown scene save error";
			}
			catch (...) {
			}
		}
		m_save.reset();
		return SaveResult::Failed;
	}

	[[nodiscard]] std::string const& SaveError() const noexcept {
		return m_save_error;
	}

	bool New() {
		if (m_save) {
			return false;
		}
		m_asset = fyuu_engine::SceneAsset::Create(fyuu_engine::Scene{});
		m_save_error.clear();
		m_dirty = false;
		m_changed_during_save = false;
		return true;
	}

	[[nodiscard]] Snapshot Capture() const {
		return {
			.scene = m_asset->Get(),
			.dirty = m_dirty
		};
	}

	void Restore(Snapshot&& snapshot) {
		m_asset->Get() = std::move(snapshot.scene);
		m_dirty = snapshot.dirty;
	}

	[[nodiscard]] fyuu_engine::SceneAsset::ManagedAsset const& Asset() const noexcept {
		return m_asset;
	}

	private:
		fyuu_engine::SceneAsset::ManagedAsset m_asset;
		std::optional<fyuu_engine::SceneSaveOperation> m_save;
		std::string m_save_error;
		bool m_dirty = false;
		bool m_changed_during_save = false;
	};

}
