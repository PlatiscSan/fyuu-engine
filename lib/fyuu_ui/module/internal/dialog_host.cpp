module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <vector>
#include <cstdint>
#include <ranges>
#endif

module fyuu_ui:dialog_host_impl;
#if defined(__cpp_lib_modules)
import std;
#endif
import :dialog_host;

namespace fyuu_ui {
	DialogHost::DialogHost(LogicalTree& tree, EventBus& events) noexcept :
	    m_tree(&tree), m_events(&events) {}

	DialogHost::~DialogHost() noexcept {
		// Closing the oldest window also removes every dialog nested above it.
		if (!m_windows.empty())
			Close(m_windows.front());
	}

	LogicalNode DialogHost::Open(Window const& window) {
		// A drag begun behind the dialog must not resume after the dialog closes.
		m_events->ReleasePointer();
		auto node = m_tree->GetWindowLayer().AddChild(window);
		node.BringToFront();
		m_events->PushModalFocusScope(*m_tree, node);
		m_windows.emplace_back(node.GetID());
		return node;
	}

	void DialogHost::Activate(std::uint64_t window_id) {
		if (m_windows.empty() || m_windows.back() != window_id)
			return;
		m_events->MoveFocus(*m_tree, FocusDirection::Next);
	}

	void DialogHost::Close(std::uint64_t window_id) noexcept {
		auto const found = std::ranges::find(m_windows, window_id);
		if (found == m_windows.end())
			return;
		auto closing = std::vector<std::uint64_t>(found, m_windows.end());
		m_windows.erase(found, m_windows.end());
		for (auto iterator = closing.rbegin(); iterator != closing.rend(); ++iterator) {
			if (!m_tree->IsInSubtree(*iterator, *iterator))
				continue;
			auto node = m_tree->GetNode(*iterator);
			m_events->PopModalFocusScope(*m_tree, node);
			m_events->Remove(*m_tree, *iterator);
		}
	}

	bool DialogHost::IsOpen() const noexcept {
		return !m_windows.empty();
	}

	bool DialogHost::AllowsInput(std::uint64_t node_id) const noexcept {
		return m_windows.empty() || m_tree->IsInSubtree(m_windows.back(), node_id);
	}

	LogicalTree& DialogHost::Tree() const noexcept {
		return *m_tree;
	}

	EventBus& DialogHost::Events() const noexcept {
		return *m_events;
	}
} // namespace fyuu_ui
