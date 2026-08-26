module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>
#include <string_view>
#endif

module fyuu_ui:message_box_impl;
#if defined(__cpp_lib_modules)
import std;
#endif
import :message_box;

namespace fyuu_ui {
	MessageBox::MessageBox(DialogHost& host) noexcept :
	    m_host(&host), m_tree(&host.Tree()), m_events(&host.Events()) {}

	MessageBox::~MessageBox() noexcept {
		Close();
	}

	void MessageBox::Show(std::string_view title, std::string_view message) {
		Close();
		auto window = m_host->Open(
		    Window{
		        std::string{title},
		        {330.0f, 210.0f},
		        {420.0f, 180.0f},
		        true,
		        InteractionState::Normal,
		        {320.0f, 150.0f},
		        false
		    }
		);
		m_window_id = window.GetID();
		m_open = true;

		auto content = window.AddChild(Overlay{true});
		m_content_id = content.GetID();
		LayoutProperties content_layout;
		content_layout.margin = {16.0f, 16.0f, 16.0f, 14.0f};
		content.SetLayout(content_layout);
		auto text = content.AddChild(
		    TextBlock{std::string{message}, {0.875f, 0.890f, 0.920f, 1.0f}, 14.0f}
		);
		m_text_id = text.GetID();
		LayoutProperties text_layout;
		text_layout.margin.bottom = 42.0f;
		text.SetLayout(text_layout);
		auto button = content.AddChild(Button{"OK", true, true});
		m_button_id = button.GetID();
		LayoutProperties button_layout;
		button_layout.width = 76.0f;
		button_layout.height = 30.0f;
		button_layout.horizontal_alignment = Alignment::End;
		button_layout.vertical_alignment = Alignment::End;
		button.SetLayout(button_layout);
		m_events->Subscribe<ClickEvent>(button,
			[this](ClickEvent& event) {
				m_close_pending = true;
				event.handled = true;
			}
		);
		m_events->Subscribe<KeyDownEvent>(button, [this](KeyDownEvent& event) {
			// Enter and Escape are content-level choices; DialogHost only guarantees
			// that the key cannot escape to the obscured window beneath this one.
			if (event.key == Key::Enter || event.key == Key::Escape) {
				m_close_pending = true;
				event.handled = true;
			}
		});
		m_host->Activate(m_window_id);
	}

	void MessageBox::Update() {
		if (m_close_pending) {
			Close();
		}
	}

	void MessageBox::Close() {
		m_close_pending = false;
		if (!m_open) {
			return;
		}
		auto const window_id = m_window_id;
		m_window_id = 0u;
		m_content_id = 0u;
		m_text_id = 0u;
		m_button_id = 0u;
		m_open = false;
		m_host->Close(window_id);
	}

	bool MessageBox::IsOpen() const noexcept {
		return m_open;
	}
	bool MessageBox::OwnsWindow(std::uint64_t node_id) const noexcept {
		return m_open && node_id == m_window_id;
	}
	bool MessageBox::SetInteraction(std::uint64_t node_id, InteractionState state) {
		if (!m_open || node_id != m_button_id) {
			return false;
		}
		m_tree->GetNode(node_id).AsWidget<Button>().interaction = state;
		return true;
	}
	void MessageBox::ClearInteraction() {
		if (m_open) {
			m_tree->GetNode(m_button_id).AsWidget<Button>().interaction = InteractionState::Normal;
		}
	}
} // namespace fyuu_ui
