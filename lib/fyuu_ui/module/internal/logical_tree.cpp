module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <variant>
#include <span>
#endif // !defined(__cpp_lib_modules)

module fyuu_ui:logical_tree_impl;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :logical_tree;

namespace fyuu_ui {

	LogicalNode LogicalTree::InsertNode(std::variant<Container, Widget> const& content) {
		// Reuse tombstones before growing the stable-slot array. The returned node is
		// intentionally detached; AddChild performs all hierarchy validation.
		auto is_empty = [](std::optional<NodeState> const& state) {
			return !state;
		};
		auto const empty_state = std::ranges::find_if(m_nodes, is_empty);
		if (empty_state != m_nodes.end()) {
			auto const id = static_cast<std::uint64_t>(std::distance(m_nodes.begin(), empty_state));
			*empty_state = NodeState{content, {}, {}, DetachedNode, 0u, 0u};
			return LogicalNode{PassKey<LogicalTree>{}, this, id};
		}
		auto const id = static_cast<std::uint64_t>(m_nodes.size());
		m_nodes.emplace_back(NodeState{content, {}, {}, DetachedNode, 0u, 0u});
		return LogicalNode{PassKey<LogicalTree>{}, this, id};
	}

	std::vector<std::uint64_t> LogicalTree::Ancestors(std::uint64_t source_id) const {
		// Routes are source-first because that is Bubble's natural iteration order.
		std::vector<std::uint64_t> route;
		auto current_id = source_id;
		// A valid parent chain cannot contain more nodes than the slot array. Besides
		// giving the loop a concrete bound, this turns accidental cycles into a clear
		// invariant failure instead of hanging event dispatch.
		for (std::size_t depth = 0u; depth < m_nodes.size(); ++depth) {
			if (current_id >= m_nodes.size() || !m_nodes[current_id]) {
				throw std::out_of_range{"The routed event source does not exist."};
			}
			route.emplace_back(current_id);
			if (current_id == 0u) {
				return route;
			}
			if (m_nodes[current_id]->parent == DetachedNode) {
				throw std::logic_error{"A detached logical node cannot route an event."};
			}
			current_id = m_nodes[current_id]->parent;
		}
		throw std::logic_error{"The logical hierarchy contains a parent cycle."};
	}

	void LogicalTree::ActivateWindow(std::uint64_t layer_id, std::uint64_t window_id) noexcept {
		// Do not trust previous active flags: rewrite every direct Window child so the
		// "at most one active window" invariant is restored in one place.
		auto child_id = m_nodes[layer_id]->child;
		while (child_id != 0u) {
			if (auto* widget = std::get_if<Widget>(&m_nodes[child_id]->content)) {
				if (auto* window = std::get_if<Window>(widget)) {
					window->active = child_id == window_id;
				}
			}
			child_id = m_nodes[child_id]->sibling;
		}
	}

	LogicalTree::LogicalTree(Container const& root) {
		// The root permanently occupies ID zero and is never detached or removed.
		m_nodes.emplace_back(NodeState{root, {}, {}, DetachedNode, 0u, 0u});
		if (std::holds_alternative<WindowLayer>(root)) {
			m_window_layer_id = 1u;
		}
	}

	bool LogicalTree::IsAttached(
	    PassKey<EventBus>,
	    std::uint64_t node_id
	) const noexcept {
		return node_id != 0u && node_id < m_nodes.size() && m_nodes[node_id] &&
		    m_nodes[node_id]->parent != DetachedNode;
	}

	bool LogicalTree::IsFocusable(
	    PassKey<EventBus> key,
	    std::uint64_t node_id
	) const noexcept {
		if (!IsAttached(key, node_id))
			return false;
		auto const* widget = std::get_if<Widget>(&m_nodes[node_id]->content);
		if (widget == nullptr)
			return false;
		return std::visit(
		    [](auto const& value) {
			    using Value = std::remove_cvref_t<decltype(value)>;
			    if constexpr (std::same_as<Value, Button> || std::same_as<Value, FileItem> ||
			        std::same_as<Value, ToggleButton> || std::same_as<Value, CheckBox>) {
				    return value.enabled;
			    } else if constexpr (std::same_as<Value, TextBox> ||
			        std::same_as<Value, NumericBox>) {
				    return !value.read_only;
			    } else {
				    return std::same_as<Value, Slider> || std::same_as<Value, MenuBar>;
			    }
		    },
		    *widget
		);
	}

	bool LogicalTree::IsInSubtree(
	    std::uint64_t ancestor_id,
	    std::uint64_t node_id
	) const noexcept {
		if (node_id >= m_nodes.size() || !m_nodes[node_id])
			return false;
		auto current_id = node_id;
		for (std::size_t depth = 0u; depth < m_nodes.size(); ++depth) {
			if (current_id == ancestor_id)
				return true;
			if (current_id == 0u || current_id >= m_nodes.size() || !m_nodes[current_id])
				return false;
			auto const parent_id = m_nodes[current_id]->parent;
			if (parent_id == DetachedNode)
				return false;
			current_id = parent_id;
		}
		return false;
	}

	std::vector<std::uint64_t> LogicalTree::BuildFocusOrder(
	    PassKey<EventBus> key,
	    std::uint64_t root_id
	) const {
		std::vector<std::uint64_t> result;
		if (root_id >= m_nodes.size() || !m_nodes[root_id])
			return result;
		std::vector<std::uint64_t> pending{root_id};
		while (!pending.empty()) {
			auto const node_id = pending.back();
			pending.pop_back();
			if (IsFocusable(key, node_id))
				result.emplace_back(node_id);
			std::vector<std::uint64_t> children;
			auto child_id = m_nodes[node_id]->child;
			while (child_id != 0u) {
				children.emplace_back(child_id);
				child_id = m_nodes[child_id]->sibling;
			}
			for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator)
				pending.emplace_back(*iterator);
		}
		return result;
	}

	void LogicalTree::SetFocused(
	    PassKey<EventBus>,
	    std::uint64_t node_id,
	    bool focused
	) noexcept {
		if (node_id >= m_nodes.size() || !m_nodes[node_id])
			return;
		auto* widget = std::get_if<Widget>(&m_nodes[node_id]->content);
		if (widget == nullptr)
			return;
		std::visit(
		    [focused](auto& value) {
			    using Value = std::remove_cvref_t<decltype(value)>;
			    if constexpr (std::same_as<Value, TextBox> || std::same_as<Value, NumericBox>) {
				    value.focused = focused;
				    if constexpr (std::same_as<Value, TextBox>) {
					    if (focused) {
						    CollapseTextSelection(value, value.text.size());
						    BeginTextEdit(value);
					    }
				    }
			    }
		    },
		    *widget
		);
	}

	void LogicalTree::SetLayout(
	    PassKey<LogicalNode>,
	    std::uint64_t id,
	    LayoutProperties const& layout
	) {
		if (id >= m_nodes.size() || !m_nodes[id]) {
			throw std::out_of_range{"The logical node does not exist."};
		}
		m_nodes[id]->layout = layout;
	}

	void LogicalTree::SetStyle(PassKey<LogicalNode>, std::uint64_t id, StyleOverride const& style) {
		if (id >= m_nodes.size() || !m_nodes[id]) {
			throw std::out_of_range{"The logical node does not exist."};
		}
		m_nodes[id]->style = style;
	}

	void LogicalTree::BringToFront(PassKey<LogicalNode>, std::uint64_t id) {
		if (id == 0u || id >= m_nodes.size() || !m_nodes[id]) {
			throw std::out_of_range{"The logical node does not exist."};
		}
		if (m_nodes[id]->parent == DetachedNode) {
			throw std::logic_error{"The logical node is not attached."};
		}
		auto const parent_id = m_nodes[id]->parent;
		auto* parent = std::get_if<Container>(&m_nodes[parent_id]->content);
		auto* widget = std::get_if<Widget>(&m_nodes[id]->content);
		if (parent != nullptr && std::holds_alternative<WindowLayer>(*parent) &&
		    widget != nullptr && std::holds_alternative<Window>(*widget)) {
			ActivateWindow(parent_id, id);
		}
		// Already the tail: activation above may still have changed window state, but
		// no sibling links need rewriting.
		if (m_nodes[id]->sibling == 0u) {
			return;
		}
		// First unlink id from its current position, then append it to the tail. The
		// node keeps the same stable ID and all descendants remain untouched.
		if (m_nodes[parent_id]->child == id) {
			m_nodes[parent_id]->child = m_nodes[id]->sibling;
		} else {
			auto previous_id = m_nodes[parent_id]->child;
			while (m_nodes[previous_id]->sibling != id) {
				previous_id = m_nodes[previous_id]->sibling;
			}
			m_nodes[previous_id]->sibling = m_nodes[id]->sibling;
		}
		auto last_id = m_nodes[parent_id]->child;
		while (m_nodes[last_id]->sibling != 0u) {
			last_id = m_nodes[last_id]->sibling;
		}
		m_nodes[last_id]->sibling = id;
		m_nodes[id]->sibling = 0u;
	}

	void LogicalTree::AddChild(
	    PassKey<LogicalNode>,
	    LogicalNode const* parent,
	    LogicalNode const& child
	) {
		auto const parent_id = parent->GetID();
		auto const child_id = child.GetID();
		if (parent_id >= m_nodes.size() || !m_nodes[parent_id] || child_id >= m_nodes.size() ||
		    !m_nodes[child_id]) {
			throw std::out_of_range{"The logical node does not exist."};
		}
		if (parent_id == child_id || m_nodes[child_id]->parent != DetachedNode) {
			throw std::logic_error{"The logical node cannot be attached here."};
		}
		auto* parent_container = std::get_if<Container>(&m_nodes[parent_id]->content);
		// WindowLayer is a semantic boundary, not a general-purpose overlay: only
		// Window widgets may be its direct children.
		auto const is_window_layer =
		    parent_container != nullptr && std::holds_alternative<WindowLayer>(*parent_container);
		if (is_window_layer) {
			auto* child_widget = std::get_if<Widget>(&m_nodes[child_id]->content);
			if (child_widget == nullptr || !std::holds_alternative<Window>(*child_widget)) {
				throw std::logic_error{"WindowLayer accepts only Window child nodes."};
			}
		}
		// Commit the parent link only after all validation has succeeded. Children are
		// appended so sibling order remains both insertion and painting order.
		m_nodes[child_id]->parent = parent_id;
		if (m_nodes[parent_id]->child == 0u) {
			m_nodes[parent_id]->child = child_id;
		} else {
			auto sibling_id = m_nodes[parent_id]->child;
			while (m_nodes[sibling_id]->sibling != 0u) {
				sibling_id = m_nodes[sibling_id]->sibling;
			}
			m_nodes[sibling_id]->sibling = child_id;
		}
		if (is_window_layer) {
			// A newly added window is topmost, so it becomes the sole active window.
			ActivateWindow(parent_id, child_id);
		}
	}

	LogicalNode LogicalTree::Insert(PassKey<LogicalNode>, Container const& container) {
		if (std::holds_alternative<WindowLayer>(container) && m_window_layer_id != 0u) {
			throw std::logic_error{"LogicalTree accepts at most one WindowLayer."};
		}
		auto node = InsertNode(container);
		if (std::holds_alternative<WindowLayer>(container)) {
			m_window_layer_id = node.GetID() + 1u;
		}
		return node;
	}

	LogicalNode LogicalTree::GetRoot() noexcept {
		return LogicalNode{PassKey<LogicalTree>{}, this, 0u};
	}

	LogicalNode LogicalTree::Insert(PassKey<LogicalNode>, Widget const& widget) {
		return InsertNode(widget);
	}

	void LogicalTree::Remove(std::uint64_t id) noexcept {
		// Node zero is the permanent tree root. Missing IDs are deliberately a no-op so
		// callers can safely close an object whose lifetime may already have ended.
		if (id == 0u || id >= m_nodes.size() || !m_nodes[id]) {
			return;
		}

		// Collect while the subtree is still linked. EventBus needs the original
		// document order to transfer focus to the next surviving node deterministically.
		std::vector<std::uint64_t> subtree{id};
		for (std::size_t index = 0u; index < subtree.size(); ++index) {
			auto child_id = m_nodes[subtree[index]]->child;
			while (child_id != 0u) {
				subtree.emplace_back(child_id);
				child_id = m_nodes[child_id]->sibling;
			}
		}
		// DetachedNode means that Insert() created this node but it has not been
		// attached yet. Only attached nodes participate in a sibling list.
		auto const parent_id = m_nodes[id]->parent;
		auto const* removed_widget = std::get_if<Widget>(&m_nodes[id]->content);
		auto const* removed_window =
		    removed_widget == nullptr ? nullptr : std::get_if<Window>(removed_widget);
		auto const activate_previous_window = removed_window != nullptr && removed_window->active;
		if (parent_id != DetachedNode) {
			auto const sibling_id = m_nodes[id]->sibling;

			// Detach the subtree root without touching its descendants. Descendant links
			// remain intact until the breadth-first collection below has discovered them.
			if (m_nodes[parent_id]->child == id) {
				m_nodes[parent_id]->child = sibling_id;
			} else {
				auto previous_id = m_nodes[parent_id]->child;
				while (m_nodes[previous_id]->sibling != id) {
					previous_id = m_nodes[previous_id]->sibling;
				}
				m_nodes[previous_id]->sibling = sibling_id;
			}

			// WindowLayer paints later siblings on top. If its active window was removed,
			// promote the new topmost window and let ActivateWindow enforce uniqueness.
			if (activate_previous_window && m_nodes[parent_id]->child != 0u) {
				auto active_id = m_nodes[parent_id]->child;
				while (m_nodes[active_id]->sibling != 0u) {
					active_id = m_nodes[active_id]->sibling;
				}
				ActivateWindow(parent_id, active_id);
			}
		}

		// Node slots are stable and never renumbered. Clear every side table together
		// with the slot so stale event callbacks and the WindowLayer singleton marker
		// cannot outlive the logical nodes they refer to.
		for (auto const node_id : subtree) {
			if (m_window_layer_id == node_id + 1u) {
				m_window_layer_id = 0u;
			}
			m_nodes[node_id].reset();
		}
	}

	LogicalNode LogicalTree::GetNode(std::uint64_t id) {
		if (id >= m_nodes.size() || !m_nodes[id]) {
			throw std::out_of_range{"The logical node does not exist."};
		}
		return LogicalNode{PassKey<LogicalTree>{}, this, id};
	}

	LogicalNode LogicalTree::GetWindowLayer() {
		if (m_window_layer_id == 0u)
			throw std::logic_error{"The logical tree has no WindowLayer."};
		return GetNode(m_window_layer_id - 1u);
	}

	LogicalNode LogicalNode::AddChild(Widget const& widget) {
		auto child = m_tree->Insert(PassKey<LogicalNode>{}, widget);
		m_tree->AddChild(PassKey<LogicalNode>{}, this, child);
		return child;
	}

	LogicalNode LogicalNode::AddChild(Container const& container) {
		auto child = m_tree->Insert(PassKey<LogicalNode>{}, container);
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

} // namespace fyuu_ui
