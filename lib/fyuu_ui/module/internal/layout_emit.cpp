module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <vector>
#include <cstdint>
#include <optional>
#include <variant>
#include <span>
#endif

module fyuu_ui:layout_emit_impl;
#if defined(__cpp_lib_modules)
import std;
#endif
import :emitter;

namespace fyuu_ui {

	VisualTree LogicalTree::EmitVisualTree(
	    std::span<std::uint64_t const> order,
	    std::span<LayoutResult const> layout,
	    std::span<std::optional<Color> const> foregrounds,
	    std::span<std::optional<float> const> font_sizes,
	    Theme const& theme,
	    TextMeasurer const& measure_text
	) const {
		// This file owns traversal only. Concrete visual policy lives in emit/*.cpp.
		VisualTree result{PassKey<LogicalTree>{}};
		std::vector<std::uint64_t> anchors(m_nodes.size());
		for (auto const node_id : order) {
			if (node_id != 0u) {
				// Layout bounds are absolute. Visual transforms are parent-relative so a
				// subtree can be moved without rewriting every descendant visual.
				auto const parent_id = m_nodes[node_id]->parent;
				auto const& bounds = layout[node_id].bounds;
				auto const& parent_bounds = layout[parent_id].bounds;
				auto const transform = Visual{TransformVisual{Transform2D{
				    {bounds.position.x - parent_bounds.position.x,
				        bounds.position.y - parent_bounds.position.y}
				}}};
				auto const visual_id = result.Insert(PassKey<LogicalTree>{}, node_id, transform);
				result.AddChild(PassKey<LogicalTree>{}, anchors[parent_id], visual_id);
				anchors[node_id] = visual_id;
			}

			auto const first_child = m_nodes[node_id]->child;
			auto const first_child_size =
			    first_child == 0u ? Size{} : layout[first_child].bounds.size;
			// Emitters are pure visual-fragment producers. Only this traversal mutates
			// VisualTree or translates fragment-local parent indices into tree IDs.
			auto output = std::visit(
			    [&](auto const& category) {
				    return std::visit(
				        [&](auto const& value) {
					        return detail::Emit(
					            Rect{{}, layout[node_id].bounds.size},
					            m_nodes[node_id]->style,
					            foregrounds[node_id],
					            font_sizes[node_id],
					            theme,
					            measure_text,
					            first_child != 0u && m_nodes[first_child]->sibling != 0u,
					            first_child_size,
					            value
					        );
				        },
				        category
				    );
			    },
			    m_nodes[node_id]->content
			);
			std::vector<std::uint64_t> visual_ids;
			visual_ids.reserve(output.entries.size());
			for (auto const& entry : output.entries) {
				auto const visual_id = result.Insert(PassKey<LogicalTree>{}, node_id, entry.visual);
				auto const parent_id = entry.parent ? visual_ids[*entry.parent] : anchors[node_id];
				result.AddChild(PassKey<LogicalTree>{}, parent_id, visual_id);
				visual_ids.emplace_back(visual_id);
			}
			if (output.child_anchor)
				anchors[node_id] = visual_ids[*output.child_anchor];
		}
		return result;
	}

} // namespace fyuu_ui
