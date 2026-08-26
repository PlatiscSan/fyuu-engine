module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <vector>
#include <cstdint>
#endif

export module fyuu_ui:dialog_host;
#if defined(__cpp_lib_modules)
import std;
#endif
import :logical_tree;
import :event_bus;

export namespace fyuu_ui {
	/// Owns modal-window lifetime and the input boundary shared by every dialog.
	/// Dialog implementations build only their Window content; this host attaches
	/// it to the window layer, maintains nesting order, and constrains focus/input
	/// to the topmost dialog until that dialog closes.
	class DialogHost final {
	private:
		LogicalTree* m_tree;
		EventBus* m_events;
		std::vector<std::uint64_t> m_windows;

	public:
		DialogHost(LogicalTree& tree, EventBus& events) noexcept;
		~DialogHost() noexcept;

		DialogHost(DialogHost const&) = delete;
		DialogHost& operator=(DialogHost const&) = delete;
		DialogHost(DialogHost&&) = delete;
		DialogHost& operator=(DialogHost&&) = delete;

		/// Attaches one complete dialog window and makes it the active modal scope.
		LogicalNode Open(Window const& window);
		/// Selects the first focusable content after the dialog has built its subtree.
		void Activate(std::uint64_t window_id);
		/// Closes this window and every dialog nested above it.
		void Close(std::uint64_t window_id) noexcept;
		[[nodiscard]] bool IsOpen() const noexcept;
		[[nodiscard]] bool AllowsInput(std::uint64_t node_id) const noexcept;
		[[nodiscard]] LogicalTree& Tree() const noexcept;
		[[nodiscard]] EventBus& Events() const noexcept;
	};
} // namespace fyuu_ui
