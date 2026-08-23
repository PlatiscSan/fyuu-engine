module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <vector>
#include <cstdint>
#include <type_traits>
#include <optional>
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_ui:visual_tree_impl;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :visual_tree;

namespace {

	struct TraversalEntry {
		std::uint64_t id;
		bool exiting = false;
	};

	struct HitTestEntry {
		std::uint64_t id;
		fyuu_ui::Point translation;
		std::optional<fyuu_ui::Rect> clip;
	};

}

namespace fyuu_ui {

	VisualTree::VisualTree(PassKey<LogicalTree>) noexcept 
		: m_nodes() {
		m_nodes.push_back(
			{
				TransformVisual{},
				0u,
				std::nullopt,
				std::nullopt,
				std::nullopt
			}
		);
	}

	std::uint64_t VisualTree::Insert(PassKey<LogicalTree>, std::uint64_t logical_id, Visual const& visual) {
		auto const id = static_cast<std::uint64_t>(m_nodes.size());
		m_nodes.push_back(
			{
				visual,
				logical_id,
				std::nullopt,
				std::nullopt,
				std::nullopt
			}
		);
		return id;
	}

	void VisualTree::AddChild(PassKey<LogicalTree>, std::uint64_t parent_id, std::uint64_t child_id) {
		if (parent_id >= m_nodes.size() ||
			child_id >= m_nodes.size()) {
			throw std::out_of_range{ "The visual node does not exist." };
		}
		if (parent_id == child_id || m_nodes[child_id].parent.has_value()) {
			throw std::logic_error{ "The visual node cannot be attached here." };
		}
		m_nodes[child_id].parent = parent_id;
		if (!m_nodes[parent_id].child.has_value()) {
			m_nodes[parent_id].child = child_id;
			return;
		}
		auto sibling_id = *m_nodes[parent_id].child;
		while (m_nodes[sibling_id].sibling.has_value()) {
			sibling_id = *m_nodes[sibling_id].sibling;
		}
		m_nodes[sibling_id].sibling = child_id;
	}

	std::vector<DrawCommand> VisualTree::WriteDrawList() const {
		std::vector<DrawCommand> result;
		result.reserve(m_nodes.size() * 2u);
		std::vector<TraversalEntry> traversal{ TraversalEntry{ 0u } };
		while (!traversal.empty()) {
			auto const entry = traversal.back();
			traversal.pop_back();
			auto const& visual = m_nodes[entry.id].visual;
			if (entry.exiting) {
				if (std::holds_alternative<TransformVisual>(visual)) {
					result.emplace_back(PopTransformCommand{});
				}
				else if (std::holds_alternative<ClipVisual>(visual)) {
					result.emplace_back(PopClipCommand{});
				}
				continue;
			}

			std::visit(
				[&result](auto const& state) {
					using State = std::remove_cvref_t<decltype(state)>;
					if constexpr (std::same_as<State, RectangleVisual>) {
						result.emplace_back(
							DrawRectangleCommand{
								state.bounds,
								state.fill
							}
						);
					}
					else if constexpr (std::same_as<State, GradientRectangleVisual>) {
						result.emplace_back(
							DrawGradientRectangleCommand{
								state.bounds,
								state.top,
								state.bottom
							}
						);
					}
					else if constexpr (std::same_as<State, LineVisual>) {
						result.emplace_back(
							DrawLineCommand{
								state.start,
								state.end,
								state.color,
								state.thickness
							}
						);
					}
					else if constexpr (std::same_as<State, HitTestVisual>) {
						return;
					}
					else if constexpr (std::same_as<State, TextVisual>) {
						result.emplace_back(
							DrawTextCommand{
								state.bounds,
								state.text,
								state.color,
								state.font_size,
								state.caret_offset
							}
						);
					}
					else if constexpr (std::same_as<State, ClipVisual>) {
						result.emplace_back(PushClipCommand{ state.bounds });
					}
					else {
						result.emplace_back(PushTransformCommand{ state.transform });
					}
				},
				visual
			);

			if (std::holds_alternative<TransformVisual>(visual) || std::holds_alternative<ClipVisual>(visual)) {
				traversal.emplace_back(TraversalEntry{ entry.id, true });
			}
			std::vector<std::uint64_t> children;
			auto child_id = m_nodes[entry.id].child;
			while (child_id.has_value()) {
				children.emplace_back(*child_id);
				child_id = m_nodes[*child_id].sibling;
			}
			for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator) {
				traversal.emplace_back(TraversalEntry{ *iterator });
			}
		}
		return result;
	}

	std::optional<HitTestResult> VisualTree::HitTest(Point point) const {
		std::optional<HitTestResult> result;
		std::vector<HitTestEntry> traversal{ HitTestEntry{ 0u, {}, std::nullopt } };
		while (!traversal.empty()) {
			auto entry = traversal.back();
			traversal.pop_back();
			auto const& visual = m_nodes[entry.id].visual;
			if (auto const* transform = std::get_if<TransformVisual>(&visual)) {
				entry.translation.x += transform->transform.translation.x;
				entry.translation.y += transform->transform.translation.y;
			}
			else if (auto const* clip = std::get_if<ClipVisual>(&visual)) {
				auto bounds = clip->bounds;
				bounds.position.x += entry.translation.x;
				bounds.position.y += entry.translation.y;
				if (entry.clip.has_value()) {
					auto const left = std::max(entry.clip->position.x, bounds.position.x);
					auto const top = std::max(entry.clip->position.y, bounds.position.y);
					auto const right = std::min(
					entry.clip->position.x + entry.clip->size.width,
					bounds.position.x + bounds.size.width
				);
					auto const bottom = std::min(
					entry.clip->position.y + entry.clip->size.height,
					bounds.position.y + bounds.size.height
				);
					entry.clip = Rect{
						{ left, top },
						{ std::max(0.0f, right - left), std::max(0.0f, bottom - top) }
					};
				}
				else {
					entry.clip = bounds;
				}
			}
			else {
				auto hit_test_role = HitTestRole::Content;
				auto resize_region = WindowResizeRegion::None;
				auto bounds = std::visit(
					[&hit_test_role, &resize_region](auto const& state) {
						using State = std::remove_cvref_t<decltype(state)>;
						if constexpr (std::same_as<State, RectangleVisual>) {
							hit_test_role = state.hit_test_role;
							return state.bounds;
						}
						else if constexpr (std::same_as<State, GradientRectangleVisual>) {
							hit_test_role = state.hit_test_role;
							return state.bounds;
						}
						else if constexpr (std::same_as<State, TextVisual>) {
							return state.bounds;
						}
						else if constexpr (std::same_as<State, HitTestVisual>) {
							hit_test_role = state.role;
							resize_region = state.resize_region;
							return state.bounds;
						}
						else {
							return Rect{};
						}
					},
					visual
				);
				bounds.position.x += entry.translation.x;
				bounds.position.y += entry.translation.y;
				auto const inside_bounds = point.x >= bounds.position.x &&
					point.y >= bounds.position.y &&
					point.x < bounds.position.x + bounds.size.width &&
					point.y < bounds.position.y + bounds.size.height;
				auto inside_clip = true;
				if (entry.clip.has_value()) {
					inside_clip = point.x >= entry.clip->position.x &&
						point.y >= entry.clip->position.y &&
						point.x < entry.clip->position.x + entry.clip->size.width &&
						point.y < entry.clip->position.y + entry.clip->size.height;
				}
				if (inside_bounds && inside_clip) {
					if (!result.has_value() ||
						result->logical_id != m_nodes[entry.id].logical_id ||
						hit_test_role != HitTestRole::Content) {
						result = HitTestResult{
							m_nodes[entry.id].logical_id,
							{
								point.x - bounds.position.x,
								point.y - bounds.position.y
							},
							bounds.size,
							hit_test_role,
							resize_region
						};
					}
				}
			}
			std::vector<std::uint64_t> children;
			auto child_id = m_nodes[entry.id].child;
			while (child_id.has_value()) {
				children.emplace_back(*child_id);
				child_id = m_nodes[*child_id].sibling;
			}
			for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator) {
				traversal.emplace_back(HitTestEntry{ *iterator, entry.translation, entry.clip });
			}
		}
		return result;
	}

}
