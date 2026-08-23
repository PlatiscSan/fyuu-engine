module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <memory>
#include <utility>

#include <vector>

#include <string>
#endif // !defined(__cpp_lib_modules)

module fyuu_studio:model_commands;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :model;

namespace fyuu_studio::model {

	/// One reversible scene mutation. Commands capture enough state to apply,
	/// undo, and reapply the change; the arena's tombstone design keeps handles
	/// stable across all three transitions.
	class Command {
	public:
		virtual ~Command() noexcept = default;
		virtual void Execute(SceneGraph& scene) = 0;
		virtual void Undo(SceneGraph& scene) = 0;
		virtual void Redo(SceneGraph& scene) = 0;
	};

	/// Creates one entity under an optional parent. Undo marks the new slot dead;
	/// redo reactivates it, so the returned handle stays valid for its whole life.
	class CreateEntityCommand final : public Command {
	private:
		std::string m_name;
		EntityID m_parent;
		EntityID m_created;

	public:
		CreateEntityCommand(std::string name, EntityID parent)
			: m_name(std::move(name)), m_parent(parent) {
		}

		void Execute(SceneGraph& scene) override {
			m_created = scene.Create(m_name, m_parent);
		}

		void Undo(SceneGraph& scene) override {
			scene.Deactivate(m_created);
		}

		void Redo(SceneGraph& scene) override {
			scene.Reactivate(m_created);
		}

		/// Valid after Execute; the id of the created entity.
		[[nodiscard]] EntityID Created() const noexcept {
			return m_created;
		}
	};

	/// Removes an entity and its whole subtree. Captures the exact slot set on
	/// Execute; undo reactivates those slots, restoring every handle and link.
	class RemoveEntityCommand final : public Command {
	private:
		EntityID m_root;
		std::vector<EntityID> m_subtree;

	public:
		explicit RemoveEntityCommand(EntityID root)
			: m_root(root) {
		}

		void Execute(SceneGraph& scene) override {
			m_subtree = scene.CollectSubtree(m_root);
			scene.Remove(m_root);
		}

		void Undo(SceneGraph& scene) override {
			for (auto const id : m_subtree) {
				scene.Reactivate(id);
			}
		}

		void Redo(SceneGraph& scene) override {
			for (auto const id : m_subtree) {
				scene.Deactivate(id);
			}
		}
	};

	class SetTransformCommand final : public Command {
	private:
		EntityID m_id;
		Transform m_before;
		Transform m_after;

	public:
		SetTransformCommand(EntityID id, Transform before, Transform after)
			: m_id(id), m_before(before), m_after(after) {
		}

		void Execute(SceneGraph& scene) override {
			scene.SetLocalTransform(m_id, m_after);
		}

		void Undo(SceneGraph& scene) override {
			scene.SetLocalTransform(m_id, m_before);
		}

		void Redo(SceneGraph& scene) override {
			scene.SetLocalTransform(m_id, m_after);
		}
	};

	class RenameCommand final : public Command {
	private:
		EntityID m_id;
		std::string m_before;
		std::string m_after;

	public:
		RenameCommand(EntityID id, std::string before, std::string after)
			: m_id(id), m_before(std::move(before)), m_after(std::move(after)) {
		}

		void Execute(SceneGraph& scene) override {
			scene.Rename(m_id, m_after);
		}

		void Undo(SceneGraph& scene) override {
			scene.Rename(m_id, m_before);
		}

		void Redo(SceneGraph& scene) override {
			scene.Rename(m_id, m_after);
		}
	};

	class SetVisibleCommand final : public Command {
	private:
		EntityID m_id;
		bool m_before;
		bool m_after;

	public:
		SetVisibleCommand(EntityID id, bool before, bool after)
			: m_id(id), m_before(before), m_after(after) {
		}

		void Execute(SceneGraph& scene) override {
			scene.SetVisible(m_id, m_after);
		}

		void Undo(SceneGraph& scene) override {
			scene.SetVisible(m_id, m_before);
		}

		void Redo(SceneGraph& scene) override {
			scene.SetVisible(m_id, m_after);
		}
	};

	/// Moves an entity to the end of a new parent's children list. Sibling order
	/// is not preserved by undo; reparent-with-position is future work.
	class ReparentCommand final : public Command {
	private:
		EntityID m_id;
		EntityID m_old_parent;
		EntityID m_new_parent;

	public:
		ReparentCommand(EntityID id, EntityID old_parent, EntityID new_parent)
			: m_id(id), m_old_parent(old_parent), m_new_parent(new_parent) {
		}

		void Execute(SceneGraph& scene) override {
			scene.Reparent(m_id, m_new_parent);
		}

		void Undo(SceneGraph& scene) override {
			scene.Reparent(m_id, m_old_parent);
		}

		void Redo(SceneGraph& scene) override {
			scene.Reparent(m_id, m_new_parent);
		}
	};

	/// Two undo/redo stacks of executed commands. A new Execute clears the redo
	/// branch, so history is a linear edit stream per document state.
	class CommandStack {
	private:
		std::vector<std::unique_ptr<Command>> m_undo;
		std::vector<std::unique_ptr<Command>> m_redo;

	public:
		CommandStack() = default;
		~CommandStack() noexcept = default;
		CommandStack(CommandStack const&) = delete;
		CommandStack& operator=(CommandStack const&) = delete;
		CommandStack(CommandStack&&) noexcept = default;
		CommandStack& operator=(CommandStack&&) noexcept = default;

		/// Applies `command`, stores it on the undo stack, and clears redo.
		/// Returns the now-owned command so the caller can read results
		/// (for example CreateEntityCommand::Created).
		Command* Execute(SceneGraph& scene, std::unique_ptr<Command> command) {
			command->Execute(scene);
			Command* const result = command.get();
			m_undo.push_back(std::move(command));
			m_redo.clear();
			return result;
		}

		[[nodiscard]] bool Undo(SceneGraph& scene) {
			if (m_undo.empty()) {
				return false;
			}
			auto command = std::move(m_undo.back());
			m_undo.pop_back();
			command->Undo(scene);
			m_redo.push_back(std::move(command));
			return true;
		}

		[[nodiscard]] bool Redo(SceneGraph& scene) {
			if (m_redo.empty()) {
				return false;
			}
			auto command = std::move(m_redo.back());
			m_redo.pop_back();
			command->Redo(scene);
			m_undo.push_back(std::move(command));
			return true;
		}

		[[nodiscard]] bool CanUndo() const noexcept {
			return !m_undo.empty();
		}

		[[nodiscard]] bool CanRedo() const noexcept {
			return !m_redo.empty();
		}

		/// Number of applied, not-yet-undone commands.
		[[nodiscard]] std::size_t Depth() const noexcept {
			return m_undo.size();
		}

		void Clear() noexcept {
			m_undo.clear();
			m_redo.clear();
		}
	};

}
