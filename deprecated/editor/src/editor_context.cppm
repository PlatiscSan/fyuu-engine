module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#endif // !defined(__cpp_lib_modules)

module fyuu_editor:context;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_engine;
import :scene;

namespace fyuu_editor {

	// Shared editor state and undo transaction coordinator.
	// Call chain: EditorApplication/panels -> EditorContext -> EditorScene.
	// BeginEdit/CommitEdit surround one logical UI gesture, collapsing frame-by-frame
	// mutations into a single undo record.

	class EditorContext {
	private:
		struct Snapshot {
			EditorScene::Snapshot scene;
			EntityID selected_entity;

			// Called by CommitEdit; delegates scene comparison to EditorScene::Snapshot and
			// selection comparison to the UUID facade.
			[[nodiscard]] bool operator==(Snapshot const& other) const noexcept {
				return scene == other.scene
					&& std::is_eq(selected_entity <=> other.selected_entity);
			}
		};

		static constexpr std::size_t HistoryLimit = 128u;

		std::optional<Snapshot> m_pending_edit;
		std::deque<Snapshot> m_undo;
		std::deque<Snapshot> m_redo;

	public:

		EditorScene scene;
		EntityID selected_entity;
		std::vector<std::string> console_messages;
		bool show_demo_window = false;

	private:
		// Called only by CommitEdit/Undo/Redo; enforces the bounded history policy before
		// transferring a snapshot into the selected deque.
		static void PushHistory(std::deque<Snapshot>& history, Snapshot&& snapshot) {
			if (history.size() >= HistoryLimit) {
				history.pop_front();
			}
			history.push_back(std::move(snapshot));
		}

		// Called at transaction and history boundaries; calls EditorScene::Capture and
		// includes selection so Undo/Redo restores editing focus.
		[[nodiscard]] Snapshot Capture() const {
			return {
				.scene = scene.Capture(),
				.selected_entity = selected_entity
			};
		}

		// Called only by Undo/Redo; calls EditorScene::Restore and restores selection.
		void Restore(Snapshot&& snapshot) {
			scene.Restore(std::move(snapshot.scene));
			selected_entity = snapshot.selected_entity;
		}

	public:
		// Called by application commands and panels; appends to the buffer consumed by
		// DrawConsolePanel on subsequent frames.
		void Log(std::string message) {
			console_messages.push_back(std::move(message));
		}

		// Called when a widget starts a logical edit. Calls Capture only for the outermost
		// begin so repeated frames preserve the original before-state.
		void BeginEdit() {
			if (!m_pending_edit) {
				m_pending_edit = Capture();
			}
		}

		// Called when a logical edit ends. Captures the after-state, pushes the before-state
		// to undo when changed, clears redo, and closes the transaction.
		void CommitEdit() {
			if (!m_pending_edit) {
				return;
			}
			auto current = Capture();
			if (*m_pending_edit != current) {
				PushHistory(m_undo, std::move(*m_pending_edit));
				m_redo.clear();
			}
			m_pending_edit.reset();
		}

		// Called by menu enablement and Undo; reads the undo deque only.
		[[nodiscard]] bool CanUndo() const noexcept {
			return !m_undo.empty();
		}

		// Called by menu enablement and Redo; reads the redo deque only.
		[[nodiscard]] bool CanRedo() const noexcept {
			return !m_redo.empty();
		}

		// Called by menu/shortcut. Capture -> PushHistory(redo) -> Restore(previous), then
		// logs the operation and cancels any incomplete widget transaction.
		void Undo() {
			if (!CanUndo()) {
				return;
			}
			PushHistory(m_redo, Capture());
			Restore(std::move(m_undo.back()));
			m_undo.pop_back();
			m_pending_edit.reset();
			Log("Undo");
		}

		// Called by menu/shortcut. Capture -> PushHistory(undo) -> Restore(next), then logs
		// the operation and cancels any incomplete widget transaction.
		void Redo() {
			if (!CanRedo()) {
				return;
			}
			PushHistory(m_undo, Capture());
			Restore(std::move(m_redo.back()));
			m_redo.pop_back();
			m_pending_edit.reset();
			Log("Redo");
		}

		// Called by EditorApplication::NewScene after lifecycle confirmation. Calls
		// EditorScene::New, then clears selection and both histories.
		bool NewScene() {
			if (!scene.New()) {
				return false;
			}
			selected_entity = {};
			m_pending_edit.reset();
			m_undo.clear();
			m_redo.clear();
			return true;
		}

		// Called by initialization/OpenPendingScene. Forwards ownership to EditorScene::Load,
		// then resets selection and histories for the new document.
		bool LoadScene(fyuu_engine::SceneAsset::ManagedAsset&& asset) {
			if (!scene.Load(std::move(asset))) {
				return false;
			}
			selected_entity = {};
			m_pending_edit.reset();
			m_undo.clear();
			m_redo.clear();
			return true;
		}
	};

}
