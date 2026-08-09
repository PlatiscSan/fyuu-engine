module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cmath>
#include <exception>
#include <format>
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
	// EditorScene is the document-layer boundary of the editor.
	// Call chain: panels/application -> EditorContext -> EditorScene -> fyuu_engine scene facade.
	// It owns the managed scene asset and the outstanding asynchronous save operation;
	// UI code must not bypass this type when changing document lifecycle state.

	using EntityID = fyuu_asset::UUID;
	using Entity = fyuu_engine::Scene::Entity;

	// Called by SceneEqual while comparing snapshots; delegates UUID comparison to the
	// asset UUID facade so editor history does not depend on the UUID backend.
	[[nodiscard]] bool EntityEqual(Entity const& left, Entity const& right) noexcept {
		return std::is_eq(left.id <=> right.id) &&
			std::is_eq(left.parent <=> right.parent) &&
			left.name == right.name &&
			left.translation == right.translation &&
			left.rotation == right.rotation &&
			left.scale == right.scale &&
			std::is_eq(left.mesh <=> right.mesh) &&
			std::is_eq(left.material <=> right.material);
	}

	// Called by Snapshot::operator== during CommitEdit; compares entities in storage
	// order and calls EntityEqual for every pair.
	[[nodiscard]] bool SceneEqual(fyuu_engine::Scene const& left, fyuu_engine::Scene const& right) noexcept {
		return std::ranges::equal(left.entities, right.entities, EntityEqual);
	}

	class EditorScene {
	private:
		fyuu_engine::SceneAsset::ManagedAsset m_asset;
		std::optional<fyuu_engine::SceneSaveOperation> m_save;
		std::string m_save_error;
		bool m_dirty = false;
		bool m_changed_during_save = false;

	public:
		enum class SaveResult {
			None,
			Succeeded,
			Failed
		};

		struct Snapshot {
			fyuu_engine::Scene scene;
			bool dirty = false;

			// Called by EditorContext::CommitEdit to decide whether an edit transaction
			// produced an undo record; calls SceneEqual for the scene payload.
			[[nodiscard]] bool operator==(Snapshot const& other) const noexcept {
				return dirty == other.dirty && SceneEqual(scene, other.scene);
			}
		};
		// Called as part of EditorContext construction; creates an unsaved in-memory
		// SceneAsset through fyuu_engine without involving the C ABI.
		EditorScene()
			: m_asset(fyuu_engine::SceneAsset::Create(fyuu_engine::Scene{})) {
		}

		// Called during editor shutdown; waits for an outstanding save so the managed
		// asset cannot be destroyed while the engine still writes it.
		~EditorScene() noexcept {
			if (m_save) {
				try {
					m_save->Wait();
				}
				catch (...) {
				}
			}
		}

		// Called by editing UI and application commands when mutable entity access is
		// required; the pointer is valid only until the scene vector reallocates.
		[[nodiscard]] Entity* FindEntity(EntityID const& id) {
			auto& entities = m_asset->Get().entities;
			auto iterator = std::ranges::find_if(
				entities,
				[&id](Entity const& entity) {
					return std::is_eq(entity.id <=> id);
				}
			);
			return iterator == entities.end() ? nullptr : &*iterator;
		}

		// Called by read-only viewport/inspector helpers; mirrors the mutable overload
		// without exposing writable state.
		[[nodiscard]] Entity const* FindEntity(EntityID const& id) const {
			auto const& entities = m_asset->Get().entities;
			auto iterator = std::ranges::find_if(
				entities,
				[&id](Entity const& entity) {
					return std::is_eq(entity.id <=> id);
				}
			);
			return iterator == entities.end() ? nullptr : &*iterator;
		}

		// Called by panels to render the scene; exposes a read-only view and keeps
		// mutations routed through EditorScene or explicit edit transactions.
		[[nodiscard]] std::vector<Entity> const& Entities() const noexcept {
			return m_asset->Get().entities;
		}

		// Called by menus, close handling, and save completion to branch on unsaved state.
		[[nodiscard]] bool Dirty() const noexcept {
			return m_dirty;
		}

		// Called by every document mutation. Also records changes made after BeginSave
		// so UpdateSave cannot incorrectly clear newer edits.
		void MarkDirty() noexcept {
			m_dirty = true;
			m_changed_during_save = m_changed_during_save || m_save.has_value();
		}

		// Called by application command guards and close handling; reports ownership of
		// an outstanding SceneSaveOperation.
		[[nodiscard]] bool Saving() const noexcept {
			return m_save.has_value();
		}

		// Called by EditorApplication after BeginSave/UpdateSave failure to retrieve the
		// most recent validation or asynchronous persistence error.
		[[nodiscard]] std::string const& SaveError() const noexcept {
			return m_save_error;
		}

		// Called by document status and fyuu_engine::SaveScene; exposes managed-asset
		// identity without transferring ownership.
		[[nodiscard]] fyuu_engine::SceneAsset::ManagedAsset const& Asset() const noexcept {
			return m_asset;
		}

		// Called by Hierarchy rendering and delete guards; performs a read-only scan of
		// the current scene and has no document side effects.
		[[nodiscard]] bool HasChildren(EntityID const& id) const noexcept {
			return std::ranges::any_of(
				m_asset->Get().entities,
				[&id](Entity const& entity) {
					return std::is_eq(entity.parent <=> id);
				}
			);
		}

		// Called by EditorApplication and Hierarchy actions inside an EditorContext edit
		// transaction; calls Scene::CreateEntity, then marks the document dirty.
		EntityID CreateEntity(std::string const& name) {
			auto& entity = m_asset->Get().CreateEntity(name);
			MarkDirty();
			return entity.id;
		}

		// Called by EditorApplication::DuplicateSelectedEntity inside one undo transaction.
		// Copies the source scene for stable traversal, iteratively creates a fresh-UUID
		// subtree in the live scene, rebuilds parent links, then calls MarkDirty once.
		EntityID DuplicateEntity(EntityID const& id) {
			auto const source_scene = m_asset->Get();
			auto const found = std::ranges::find_if(
				source_scene.entities,
				[&id](Entity const& entity) {
					return std::is_eq(entity.id <=> id);
				}
			);
			if (found == source_scene.entities.end()) {
				return {};
			}

			struct PendingDuplicate {
				EntityID source;
				EntityID parent;
				bool root;
			};
			std::vector<PendingDuplicate> pending{
				{ found->id, found->parent, true }
			};
			EntityID duplicate_id{};
			while (!pending.empty()) {
				auto const item = pending.back();
				pending.pop_back();
				auto const source = std::ranges::find_if(
					source_scene.entities,
					[&item](Entity const& entity) {
						return std::is_eq(entity.id <=> item.source);
					}
				);
				if (source == source_scene.entities.end()) {
					continue;
				}
				auto& duplicate = m_asset->Get().CreateEntity(
					item.root ? source->name + " Copy" : source->name
				);
				duplicate.parent = item.parent;
				duplicate.translation = source->translation;
				duplicate.rotation = source->rotation;
				duplicate.scale = source->scale;
				duplicate.mesh = source->mesh;
				duplicate.material = source->material;
				if (item.root) {
					duplicate_id = duplicate.id;
				}
				for (auto const& child : source_scene.entities) {
					if (std::is_eq(child.parent <=> source->id)) {
						pending.push_back({ child.id, duplicate.id, false });
					}
				}
			}
			MarkDirty();
			return duplicate_id;
		}

		// Called by delete actions from the application/inspector. Refuses non-leaf
		// deletion, erases the matching entity, and calls MarkDirty on success.
		bool DestroyEntity(EntityID const& id) {
			auto& entities = m_asset->Get().entities;
			if (std::ranges::any_of(
				entities,
				[&id](Entity const& entity) {
					return std::is_eq(entity.parent <=> id);
				}
			)) {
				return false;
			}
			auto const removed = std::erase_if(
				entities,
				[&id](Entity const& entity) {
					return std::is_eq(entity.id <=> id);
				}
			);
			if (removed != 0u) {
				MarkDirty();
			}
			return removed != 0u;
		}

		// Called by CreateChildEntity, Hierarchy drag/drop, and Inspector "Move to Root".
		// Calls FindEntity while walking ancestors to reject missing parents and cycles,
		// then updates the parent UUID and marks the document dirty.
		bool SetParent(EntityID const& id, EntityID const& parent) {
			auto* entity = FindEntity(id);
			if (!entity || std::is_eq(id <=> parent)) {
				return false;
			}
			if (!parent.IsNil() && !FindEntity(parent)) {
				return false;
			}

			auto current = parent;
			for (std::size_t depth = 0u; !current.IsNil(); ++depth) {
				if (depth >= m_asset->Get().entities.size()
					|| std::is_eq(current <=> id)) {
					return false;
				}
				auto const* ancestor = FindEntity(current);
				if (!ancestor) {
					return false;
				}
				current = ancestor->parent;
			}

			if (std::is_eq(entity->parent <=> parent)) {
				return true;
			}
			entity->parent = parent;
			MarkDirty();
			return true;
		}

		// Called by EditorApplication::SaveScene. Validates names, transforms, and scene
		// topology, then calls fyuu_engine::SaveScene and retains its async operation.
		bool BeginSave() {
			if (m_save) {
				return false;
			}
			m_save_error.clear();
			for (auto const& entity : m_asset->Get().entities) {
				if (entity.name.empty()) {
					m_save_error = std::format(
						"Entity {} has an empty name",
						entity.id.ToString()
						);
					return false;
				}
				auto const finite = [](float value) {
					return std::isfinite(value);
					};
				if (!std::ranges::all_of(entity.translation, finite)
					|| !std::ranges::all_of(entity.rotation, finite)
					|| !std::ranges::all_of(entity.scale, finite)) {
					m_save_error = std::format(
						"Entity {} has a non-finite transform",
						entity.id.ToString()
						);
					return false;
				}
			}
			if (!m_asset->Get().Valid()) {
				m_save_error = "Scene hierarchy or entity identifiers are invalid";
				return false;
			}
			m_changed_during_save = false;
			m_save.emplace(fyuu_engine::SaveScene(m_asset));
			return true;
		}

		// Called once per frame by EditorApplication::UpdateSave. Polls IsDone, calls Wait
		// to observe errors, updates dirty state, and releases the completed operation.
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

		// Called by EditorContext::NewScene after save confirmation; replaces the managed
		// asset with a fresh scene and intentionally marks the new document dirty.
		bool New() {
			if (m_save) {
				return false;
			}
			m_asset = fyuu_engine::SceneAsset::Create(fyuu_engine::Scene{});
			m_save_error.clear();
			m_dirty = true;
			m_changed_during_save = false;
			return true;
		}

		// Called by EditorContext::LoadScene after fyuu_engine::LoadScene; takes ownership
		// of the managed asset and resets save/dirty bookkeeping.
		bool Load(fyuu_engine::SceneAsset::ManagedAsset&& asset) {
			if (m_save || !asset) {
				return false;
			}
			m_asset = std::move(asset);
			m_save_error.clear();
			m_dirty = false;
			m_changed_during_save = false;
			return true;
		}

		// Called by EditorContext::Capture at edit boundaries and before Undo/Redo; copies
		// scene value state but not the asynchronous save operation.
		[[nodiscard]] Snapshot Capture() const {
			return {
				.scene = m_asset->Get(),
				.dirty = m_dirty
			};
		}

		// Called by EditorContext::Restore during Undo/Redo. If a save is active, forces
		// dirty so completion of the older save cannot hide the restored change.
		void Restore(Snapshot&& snapshot) {
			m_asset->Get() = std::move(snapshot.scene);
			if (m_save) {
				m_changed_during_save = true;
				m_dirty = true;
			}
			else {
				m_dirty = snapshot.dirty;
			}
		}
	};

}
