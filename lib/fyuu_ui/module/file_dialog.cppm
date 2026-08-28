module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <vector>
#include <functional>
#include <string>
#include <cstdint>
#include <string_view>
#include <filesystem>
#endif

export module fyuu_ui:file_dialog;
#if defined(__cpp_lib_modules)
import std;
#endif
import :logical_tree;
import :dialog_host;
import :message_box;

export namespace fyuu_ui {
	enum class FileDialogMode { Open, Save };

	/// One human-readable file type and its extensions without leading dots.
	/// An empty extension list accepts every regular file.
	struct FileDialogFilter {
		std::string name;
		std::vector<std::string> extensions;
	};

	struct FileDialogOptions {
		std::string title;
		std::filesystem::path initial_directory;
		std::string initial_file_name;
		std::vector<FileDialogFilter> filters;
		Point position{240.0f, 120.0f};
		Size size{640.0f, 520.0f};
	};

	/// Builds and owns a file browser beneath an existing WindowLayer.
	/// The callback receives an empty path when the user cancels. Call Update once
	/// after input dispatch so callbacks are never destroyed while they are running.
	class FileDialogue final {
	private:
		enum class PendingAction { None, Navigate, Select, Accept, Cancel };

		struct Session {
			FileDialogMode mode = FileDialogMode::Open;
			FileDialogOptions options;
			std::function<void(std::filesystem::path const&)> callback;
			std::filesystem::path directory;
			std::filesystem::path selected_path;
			std::vector<std::filesystem::path> entries;
			std::uint64_t window_id = 0u;
			std::uint64_t content_id = 0u;
			std::uint64_t path_id = 0u;
			std::uint64_t file_name_id = 0u;
			std::vector<std::uint64_t> button_ids;
			std::vector<std::pair<std::uint64_t, std::filesystem::path>> visible_entries;
			std::vector<SubscriptionHandle> subscriptions;
			PendingAction pending = PendingAction::None;
			std::filesystem::path pending_path;
		};

		DialogHost* m_host;
		LogicalTree* m_tree;
		EventBus* m_events;
		MessageBox m_message_box;
		Session m_session;
		bool m_open = false;

		void Show(
		    FileDialogMode mode,
		    FileDialogOptions const& options,
		    std::function<void(std::filesystem::path const&)> callback
		);
		void RebuildContent();
		void Complete(std::filesystem::path const& path);

	public:
		explicit FileDialogue(DialogHost& host) noexcept;
		~FileDialogue() noexcept;

		FileDialogue(FileDialogue const&) = delete;
		FileDialogue& operator=(FileDialogue const&) = delete;
		FileDialogue(FileDialogue&&) = delete;
		FileDialogue& operator=(FileDialogue&&) = delete;

		void ShowOpen(
		    FileDialogOptions const& options,
		    std::function<void(std::filesystem::path const&)> callback
		);
		void ShowSave(
		    FileDialogOptions const& options,
		    std::function<void(std::filesystem::path const&)> callback
		);

		/// Applies navigation and completion requested by the previous input pass.
		void Update();
		/// Closes the current dialog and reports cancellation. Safe when already closed.
		void Cancel();
		[[nodiscard]] bool IsOpen() const noexcept;
		/// Returns whether node_id is the window currently owned by this helper.
		[[nodiscard]] bool OwnsWindow(std::uint64_t node_id) const noexcept;
		/// Updates a dynamically created dialog button; returns false for other nodes.
		bool SetInteraction(std::uint64_t node_id, InteractionState state);
		/// Restores every dialog button to its normal visual state.
		void ClearInteractions();
		[[nodiscard]] bool OwnsTextBox(std::uint64_t node_id) const noexcept;
		void SetTextBoxFocused(std::uint64_t node_id, bool focused);
		/// Applies an edited path when node_id identifies the path editor.
		void CommitTextBox(std::uint64_t node_id);
	};
} // namespace fyuu_ui
