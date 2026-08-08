module;
#include <version>
#if !defined(__cpp_lib_modules)
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

class EditorScene {
public:
	struct Snapshot {
		fyuu_engine::Scene scene;
		bool dirty = false;

		bool operator==(Snapshot const&) const = default;
	};

		EditorScene()
			: m_asset(fyuu_engine::SceneAsset::Create(fyuu_engine::Scene{})) {
		}

		EntityId CreateEntity(std::string const& name) {
			auto& entity = m_asset->Get().CreateEntity(name);
			m_dirty = true;
			return entity.id;
		}

		bool DestroyEntity(EntityId const& id) {
			auto const removed = std::erase_if(m_asset->Get().entities, [&id](Entity const& entity) {
				return fyuu_asset::UUIDEqual(entity.id, id);
			});
			m_dirty = m_dirty || removed != 0u;
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
		}

    void MarkSaved() noexcept {
        m_dirty = false;
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
		bool m_dirty = false;
	};

}
