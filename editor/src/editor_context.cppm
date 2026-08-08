module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <optional>
#include <string>
#include <vector>
#endif // !defined(__cpp_lib_modules)

module fyuu_editor:context;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_engine;
import :scene;

namespace fyuu_editor {

struct EditorContext {
	struct Snapshot {
		EditorScene::Snapshot scene;
		EntityId selected_entity;

		[[nodiscard]] bool operator==(Snapshot const& other) const noexcept {
			return scene == other.scene
				&& fyuu_asset::UUIDEqual(selected_entity, other.selected_entity);
		}
	};

    EditorScene scene;
    EntityId selected_entity;
    std::vector<std::string> console_messages;
    bool show_demo_window = false;

    void Log(std::string message) {
        console_messages.push_back(std::move(message));
    }

	void BeginEdit() {
		if (!m_pending_edit) {
			m_pending_edit = Capture();
		}
	}

	void CommitEdit() {
		if (!m_pending_edit) {
			return;
		}
		auto current = Capture();
		if (*m_pending_edit != current) {
			m_undo.push_back(std::move(*m_pending_edit));
			m_redo.clear();
		}
		m_pending_edit.reset();
	}

	[[nodiscard]] bool CanUndo() const noexcept {
		return !m_undo.empty();
	}

	[[nodiscard]] bool CanRedo() const noexcept {
		return !m_redo.empty();
	}

	void Undo() {
		if (!CanUndo()) {
			return;
		}
		m_redo.push_back(Capture());
		Restore(std::move(m_undo.back()));
		m_undo.pop_back();
		m_pending_edit.reset();
		Log("Undo");
	}

	void Redo() {
		if (!CanRedo()) {
			return;
		}
		m_undo.push_back(Capture());
		Restore(std::move(m_redo.back()));
		m_redo.pop_back();
		m_pending_edit.reset();
		Log("Redo");
	}

private:
	[[nodiscard]] Snapshot Capture() const {
		return {
			.scene = scene.Capture(),
			.selected_entity = selected_entity
		};
	}

	void Restore(Snapshot&& snapshot) {
		scene.Restore(std::move(snapshot.scene));
		selected_entity = snapshot.selected_entity;
	}

	std::optional<Snapshot> m_pending_edit;
	std::vector<Snapshot> m_undo;
	std::vector<Snapshot> m_redo;
};

}
