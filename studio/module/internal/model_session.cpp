module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>

#include <string>

#include <optional>
#endif // !defined(__cpp_lib_modules)

module fyuu_studio:model_session;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :model;
import :model_commands;

namespace fyuu_studio::model {

	/// The editor's single write facade: a SceneGraph plus undo/redo history,
	/// a selection, and a dirty flag. UI and viewport panels read through
	/// `Scene()` and mutate only through the edit methods below so every change
	/// lands on the undo stack.
	///
	/// Dirty semantics are conservative: any executed command marks the document
	/// dirty, and only `MarkSaved()` clears it. Detecting "edited back to the
	/// saved bytes" (snapshot equality, as the deprecated editor did) is future work.
	class EditSession {
	private:
		SceneGraph m_scene;
		CommandStack m_commands;
		std::optional<EntityID> m_selection;
		bool m_dirty = false;
		std::string m_title = "Untitled Scene";

		template <class CommandT, class... Args>
		void RunCommand(Args&&... args) {
			m_commands.Execute(
				m_scene,
				std::make_unique<CommandT>(std::forward<Args>(args)...)
			);
			m_dirty = true;
		}

		void DropDeadSelection() noexcept {
			if (m_selection && !m_scene.Alive(*m_selection)) {
				m_selection.reset();
			}
		}

	public:
		EditSession() = default;
		~EditSession() noexcept = default;
		EditSession(EditSession const&) = delete;
		EditSession& operator=(EditSession const&) = delete;
		EditSession(EditSession&&) noexcept = default;
		EditSession& operator=(EditSession&&) noexcept = default;

		// -- document state -------------------------------------------------------

		[[nodiscard]] SceneGraph& Scene() noexcept {
			return m_scene;
		}

		[[nodiscard]] SceneGraph const& Scene() const noexcept {
			return m_scene;
		}

		[[nodiscard]] std::string const& Title() const noexcept {
			return m_title;
		}

		void SetTitle(std::string title) {
			m_title = std::move(title);
		}

		[[nodiscard]] bool Dirty() const noexcept {
			return m_dirty;
		}

		/// Call after a successful save. Undo/redo do not clear dirty on their own.
		void MarkSaved() noexcept {
			m_dirty = false;
		}

		/// Replaces the document with a fresh empty scene; history is discarded.
		void NewScene() {
			m_scene.Clear();
			m_commands.Clear();
			m_selection.reset();
			m_title = "Untitled Scene";
			m_dirty = true;
		}

		/// Replaces the whole scene graph from a loaded snapshot and discards history.
		/// The future fyuu_asset bridge builds a SceneGraph, then calls this.
		void LoadScene(SceneGraph scene) {
			m_scene = std::move(scene);
			m_commands.Clear();
			m_selection.reset();
			m_dirty = false;
		}

		// -- selection (not undoable) ---------------------------------------------

		[[nodiscard]] std::optional<EntityID> Selection() const noexcept {
			return m_selection;
		}

		/// Selects a live entity; `std::nullopt` clears the selection.
		bool Select(std::optional<EntityID> id) {
			if (id.has_value() && !m_scene.Alive(*id)) {
				return false;
			}
			m_selection = id;
			return true;
		}

		void ClearSelection() noexcept {
			m_selection.reset();
		}

		// -- edits (one undo step each) -------------------------------------------

		/// Creates an entity and selects it. `parent` nil means root.
		EntityID CreateEntity(std::string name, std::optional<EntityID> parent = std::nullopt) {
			auto const parent_id = parent.value_or(EntityID{});
			auto* command = m_commands.Execute(
				m_scene,
				std::make_unique<CreateEntityCommand>(std::move(name), parent_id)
			);
			m_dirty = true;
			m_selection = static_cast<CreateEntityCommand*>(command)->Created();
			return *m_selection;
		}

		/// Cascade-removes the entity and its descendants.
		bool RemoveEntity(EntityID id) {
			if (!m_scene.Alive(id)) {
				return false;
			}
			RunCommand<RemoveEntityCommand>(id);
			DropDeadSelection();
			return true;
		}

		bool SetTransform(EntityID id, Transform const& transform) {
			if (!m_scene.Alive(id)) {
				return false;
			}
			auto const before = m_scene.Get(id).local;
			RunCommand<SetTransformCommand>(id, before, transform);
			return true;
		}

		bool SetPosition(EntityID id, Float3 const& position) {
			if (!m_scene.Alive(id)) {
				return false;
			}
			auto transform = m_scene.Get(id).local;
			transform.position = position;
			return SetTransform(id, transform);
		}

		bool SetRotation(EntityID id, Quat const& rotation) {
			if (!m_scene.Alive(id)) {
				return false;
			}
			auto transform = m_scene.Get(id).local;
			transform.rotation = rotation;
			return SetTransform(id, transform);
		}

		bool SetScale(EntityID id, Float3 const& scale) {
			if (!m_scene.Alive(id)) {
				return false;
			}
			auto transform = m_scene.Get(id).local;
			transform.scale = scale;
			return SetTransform(id, transform);
		}

		bool Rename(EntityID id, std::string name) {
			if (!m_scene.Alive(id)) {
				return false;
			}
			auto const before = m_scene.Get(id).name;
			RunCommand<RenameCommand>(id, before, std::move(name));
			return true;
		}

		bool SetVisible(EntityID id, bool visible) {
			if (!m_scene.Alive(id)) {
				return false;
			}
			auto const before = m_scene.Get(id).visible;
			RunCommand<SetVisibleCommand>(id, before, visible);
			return true;
		}

		bool Reparent(EntityID id, EntityID new_parent) {
			if (!m_scene.CanReparent(id, new_parent)) {
				return false;
			}
			if (m_scene.Parent(id) == new_parent) {
				return true; // already in place; nothing to record
			}
			auto const old_parent = m_scene.Parent(id);
			RunCommand<ReparentCommand>(id, old_parent, new_parent);
			return true;
		}

		// -- undo / redo ----------------------------------------------------------

		[[nodiscard]] bool CanUndo() const noexcept {
			return m_commands.CanUndo();
		}

		[[nodiscard]] bool CanRedo() const noexcept {
			return m_commands.CanRedo();
		}

		bool Undo() {
			if (!m_commands.Undo(m_scene)) {
				return false;
			}
			DropDeadSelection();
			return true;
		}

		bool Redo() {
			if (!m_commands.Redo(m_scene)) {
				return false;
			}
			DropDeadSelection();
			return true;
		}
	};

}
