module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <vector>
#include <cstdint>
#include <optional>
#endif
module fyuu_ui:visual_tree_impl;
#if defined(__cpp_lib_modules)
import std;
#endif
import :visual_tree;
import :draw_list;
import :hit_test;

namespace fyuu_ui {
	VisualTree::VisualTree(PassKey<LogicalTree>) noexcept {
		m_nodes.push_back({TransformVisual{}, 0u, detail::DetachedVisualNode, 0u, 0u});
	}
	std::uint64_t VisualTree::Insert(PassKey<LogicalTree>, std::uint64_t logical_id, Visual const& visual) {
		auto const id = static_cast<std::uint64_t>(m_nodes.size());
		m_nodes.push_back({visual, logical_id, detail::DetachedVisualNode, 0u, 0u});
		return id;
	}
	void VisualTree::AddChild(PassKey<LogicalTree>, std::uint64_t parent_id, std::uint64_t child_id) {
		if (parent_id >= m_nodes.size() || child_id >= m_nodes.size()) throw std::out_of_range{"The visual node does not exist."};
		if (parent_id == child_id || m_nodes[child_id].parent != detail::DetachedVisualNode) throw std::logic_error{"The visual node cannot be attached here."};
		m_nodes[child_id].parent = parent_id;
		if (m_nodes[parent_id].child == 0u) { m_nodes[parent_id].child = child_id; return; }
		auto sibling_id = m_nodes[parent_id].child;
		while (m_nodes[sibling_id].sibling != 0u) sibling_id = m_nodes[sibling_id].sibling;
		m_nodes[sibling_id].sibling = child_id;
	}
	std::vector<DrawCommand> VisualTree::WriteDrawList() const { return detail::BuildDrawList(m_nodes); }
	std::optional<HitTestResult> VisualTree::HitTest(Point point) const { return detail::HitTestVisuals(m_nodes, point); }
}
