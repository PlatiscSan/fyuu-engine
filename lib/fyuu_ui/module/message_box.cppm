module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>
#include <cstdint>
#include <string_view>
#endif

export module fyuu_ui:message_box;
#if defined(__cpp_lib_modules)
import std;
#endif
import :dialog_host;

export namespace fyuu_ui {
	/// Creates a small FyuuUI-owned message window under an existing WindowLayer.
	/// It never calls a platform dialog API and stores its complete session inline.
	class MessageBox final {
	private:
		DialogHost* m_host;
		LogicalTree* m_tree;
		EventBus* m_events;
		std::uint64_t m_window_id = 0u;
		std::uint64_t m_content_id = 0u;
		std::uint64_t m_text_id = 0u;
		std::uint64_t m_button_id = 0u;
		bool m_open = false;
		bool m_close_pending = false;

	public:
		explicit MessageBox(DialogHost& host) noexcept;
		~MessageBox() noexcept;

		MessageBox(MessageBox const&) = delete;
		MessageBox& operator=(MessageBox const&) = delete;
		MessageBox(MessageBox&&) = delete;
		MessageBox& operator=(MessageBox&&) = delete;

		/// Replaces any existing message with a new modal content window.
		void Show(std::string_view title, std::string_view message);
		/// Applies a close requested by an event handler after dispatch unwinds.
		void Update();
		/// Removes the message content through DialogHost; safe when already closed.
		void Close();
		[[nodiscard]] bool IsOpen() const noexcept;
		/// Used by host window gestures to recognize this message's top-level window.
		[[nodiscard]] bool OwnsWindow(std::uint64_t node_id) const noexcept;
		/// Updates the OK button only and returns false for unrelated nodes.
		bool SetInteraction(std::uint64_t node_id, InteractionState state);
		/// Restores the OK button's transient pointer state.
		void ClearInteraction();
	};
} // namespace fyuu_ui
