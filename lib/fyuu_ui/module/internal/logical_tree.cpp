module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_ui:logical_tree_impl;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :logical_tree;

namespace fyuu_ui {

	LogicalNode LogicalTree::InsertNode(std::variant<Container, Widget> const& content) {
		auto is_empty = [](std::optional<NodeState> const& state) {
			return !state.has_value();
		};
		auto const empty_state = std::ranges::find_if(
			m_nodes,
			is_empty
		);
		if (empty_state != m_nodes.end()) {
			auto const id = static_cast<std::uint64_t>(
				std::distance(m_nodes.begin(), empty_state)
			);
			*empty_state = NodeState{
				content,
				{},
				{},
				std::nullopt,
				std::nullopt,
				std::nullopt
			};
			return LogicalNode{ PassKey<LogicalTree>{}, this, id };
		}
		auto const id = static_cast<std::uint64_t>(m_nodes.size());
		m_nodes.emplace_back(NodeState{
			content,
			{},
			{},
			std::nullopt,
			std::nullopt,
			std::nullopt
		});
		return LogicalNode{ PassKey<LogicalTree>{}, this, id };
	}

	std::vector<std::uint64_t> LogicalTree::BuildRoute(std::uint64_t source_id) const {
		std::vector<std::uint64_t> route;
		auto current_id = std::optional{ source_id };
		while (current_id.has_value()) {
			if (*current_id >= m_nodes.size() || !m_nodes[*current_id].has_value()) {
				throw std::out_of_range{ "The routed event source does not exist." };
			}
			route.emplace_back(*current_id);
			current_id = m_nodes[*current_id]->parent;
		}
		return route;
	}

	LogicalTree::LogicalTree(Container const& root) {
		m_nodes.emplace_back(NodeState{
			root,
			{},
			{},
			std::nullopt,
			std::nullopt,
			std::nullopt
		});
		if (std::holds_alternative<WindowLayer>(root)) {
			m_window_layer_id = 0u;
		}
	}

	void LogicalTree::SetLayout(
		PassKey<LogicalNode>,
		std::uint64_t id,
		LayoutProperties const& layout
	) {
		if (id >= m_nodes.size() || !m_nodes[id].has_value()) {
			throw std::out_of_range{ "The logical node does not exist." };
		}
		m_nodes[id]->layout = layout;
	}

	void LogicalTree::SetStyle(
		PassKey<LogicalNode>,
		std::uint64_t id,
		StyleOverride const& style
	) {
		if (id >= m_nodes.size() || !m_nodes[id].has_value()) {
			throw std::out_of_range{ "The logical node does not exist." };
		}
		m_nodes[id]->style = style;
	}

	void LogicalTree::BringToFront(PassKey<LogicalNode>, std::uint64_t id) {
		if (id == 0u || id >= m_nodes.size() || !m_nodes[id].has_value()) {
			throw std::out_of_range{ "The logical node does not exist." };
		}
		if (!m_nodes[id]->parent.has_value()) {
			throw std::logic_error{ "The logical node is not attached." };
		}
		auto const parent_id = *m_nodes[id]->parent;
		auto* parent = std::get_if<Container>(&m_nodes[parent_id]->content);
		auto* widget = std::get_if<Widget>(&m_nodes[id]->content);
		if (parent != nullptr &&
			std::holds_alternative<WindowLayer>(*parent) &&
			widget != nullptr &&
			std::holds_alternative<Window>(*widget)) {
			auto child_id = m_nodes[parent_id]->child;
			while (child_id.has_value()) {
				auto* child = std::get_if<Widget>(&m_nodes[*child_id]->content);
				if (child != nullptr) {
					if (auto* window = std::get_if<Window>(child)) {
						window->active = *child_id == id;
					}
				}
				child_id = m_nodes[*child_id]->sibling;
			}
		}
		if (!m_nodes[id]->sibling.has_value()) {
			return;
		}
		if (m_nodes[parent_id]->child == id) {
			m_nodes[parent_id]->child = m_nodes[id]->sibling;
		}
		else {
			auto previous_id = *m_nodes[parent_id]->child;
			while (m_nodes[previous_id]->sibling != id) {
				previous_id = *m_nodes[previous_id]->sibling;
			}
			m_nodes[previous_id]->sibling = m_nodes[id]->sibling;
		}
		auto last_id = *m_nodes[parent_id]->child;
		while (m_nodes[last_id]->sibling.has_value()) {
			last_id = *m_nodes[last_id]->sibling;
		}
		m_nodes[last_id]->sibling = id;
		m_nodes[id]->sibling.reset();
	}

	void LogicalTree::AddChild(PassKey<LogicalNode>, LogicalNode const* parent, LogicalNode const& child) {
		auto const parent_id = parent->GetID();
		auto const child_id = child.GetID();
		if (parent_id >= m_nodes.size() ||
			!m_nodes[parent_id].has_value() ||
			child_id >= m_nodes.size() ||
			!m_nodes[child_id].has_value()) {
			throw std::out_of_range{ "The logical node does not exist." };
		}
		if (parent_id == child_id || m_nodes[child_id]->parent.has_value()) {
			throw std::logic_error{ "The logical node cannot be attached here." };
		}
		auto* parent_container = std::get_if<Container>(&m_nodes[parent_id]->content);
		if (parent_container != nullptr &&
			std::holds_alternative<WindowLayer>(*parent_container)) {
			auto* child_widget = std::get_if<Widget>(&m_nodes[child_id]->content);
			if (child_widget == nullptr || !std::holds_alternative<Window>(*child_widget)) {
				throw std::logic_error{ "WindowLayer accepts only Window child nodes." };
			}
			auto sibling_id = m_nodes[parent_id]->child;
			while (sibling_id.has_value()) {
				auto* sibling = std::get_if<Widget>(&m_nodes[*sibling_id]->content);
				if (sibling != nullptr) {
					std::get<Window>(*sibling).active = false;
				}
				sibling_id = m_nodes[*sibling_id]->sibling;
			}
			std::get<Window>(*child_widget).active = true;
		}
		m_nodes[child_id]->parent = parent_id;
		if (!m_nodes[parent_id]->child.has_value()) {
			m_nodes[parent_id]->child = child_id;
			return;
		}
		auto sibling_id = *m_nodes[parent_id]->child;
		while (m_nodes[sibling_id]->sibling.has_value()) {
			sibling_id = *m_nodes[sibling_id]->sibling;
		}
		m_nodes[sibling_id]->sibling = child_id;
	}

	LogicalNode LogicalTree::Insert(PassKey<LogicalNode>, Container const& container) {
		if (std::holds_alternative<WindowLayer>(container) &&
			m_window_layer_id.has_value()) {
			throw std::logic_error{ "LogicalTree accepts at most one WindowLayer." };
		}
		auto node = InsertNode(container);
		if (std::holds_alternative<WindowLayer>(container)) {
			m_window_layer_id = node.GetID();
		}
		return node;
	}

	LogicalNode LogicalTree::GetRoot() noexcept {
		return LogicalNode{ PassKey<LogicalTree>{}, this, 0u };
	}

	LogicalNode LogicalTree::Insert(Widget widget) {
		return InsertNode(widget);
	}

	void LogicalTree::Remove(std::uint64_t id) noexcept {
		if (id == 0u || id >= m_nodes.size() || !m_nodes[id].has_value()) {
			return;
		}
		auto const parent_id = *m_nodes[id]->parent;
		auto const* removed_widget = std::get_if<Widget>(&m_nodes[id]->content);
		auto const* removed_window = removed_widget == nullptr ?
			nullptr : std::get_if<Window>(removed_widget);
		auto const activate_previous_window = removed_window != nullptr && removed_window->active;
		auto const sibling_id = m_nodes[id]->sibling;
		if (m_nodes[parent_id]->child == id) {
			m_nodes[parent_id]->child = sibling_id;
		}
		else {
			auto previous_id = *m_nodes[parent_id]->child;
			while (m_nodes[previous_id]->sibling != id) {
				previous_id = *m_nodes[previous_id]->sibling;
			}
			m_nodes[previous_id]->sibling = sibling_id;
		}
		if (activate_previous_window && m_nodes[parent_id]->child.has_value()) {
			auto active_id = *m_nodes[parent_id]->child;
			while (m_nodes[active_id]->sibling.has_value()) {
				active_id = *m_nodes[active_id]->sibling;
			}
			auto* active_widget = std::get_if<Widget>(&m_nodes[active_id]->content);
			if (active_widget != nullptr) {
				if (auto* active_window = std::get_if<Window>(active_widget)) {
					active_window->active = true;
				}
			}
		}
		std::vector<std::uint64_t> subtree{ id };
		for (std::size_t index = 0u; index < subtree.size(); ++index) {
			auto child_id = m_nodes[subtree[index]]->child;
			while (child_id.has_value()) {
				subtree.emplace_back(*child_id);
				child_id = m_nodes[*child_id]->sibling;
			}
		}
		for (auto const node_id : subtree) {
			m_events.Remove(node_id);
			if (m_window_layer_id == node_id) {
				m_window_layer_id.reset();
			}
			m_nodes[node_id].reset();
		}
	}

	LogicalNode LogicalTree::GetNode(std::uint64_t id) {
		if (id >= m_nodes.size() || !m_nodes[id].has_value()) {
			throw std::out_of_range{ "The logical node does not exist." };
		}
		return LogicalNode{ PassKey<LogicalTree>{}, this, id };
	}

	LogicalNode LogicalNode::AddChild(Widget widget) {
		auto child = m_tree->Insert(std::move(widget));
		m_tree->AddChild(PassKey<LogicalNode>{}, this, child);
		return child;
	}

	LogicalNode LogicalNode::AddChild(Container container) {
		auto child = m_tree->Insert(PassKey<LogicalNode>{}, std::move(container));
		m_tree->AddChild(PassKey<LogicalNode>{}, this, child);
		return child;
	}

	void LogicalNode::SetLayout(LayoutProperties const& layout) {
		m_tree->SetLayout(PassKey<LogicalNode>{}, m_id, layout);
	}

	void LogicalNode::SetStyle(StyleOverride const& style) {
		m_tree->SetStyle(PassKey<LogicalNode>{}, m_id, style);
	}

	void LogicalNode::BringToFront() {
		m_tree->BringToFront(PassKey<LogicalNode>{}, m_id);
	}

}
