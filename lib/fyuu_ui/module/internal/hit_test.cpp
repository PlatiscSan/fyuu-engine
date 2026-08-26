module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <type_traits>
#include <optional>
#include <variant>
#include <span>
#endif
module fyuu_ui:hit_test;
#if defined(__cpp_lib_modules)
import std;
#endif
import :visual_tree;

namespace fyuu_ui::detail {
	std::optional<HitTestResult> HitTestVisuals(std::span<VisualNode const> nodes, Point point) {
		struct Visit { std::uint64_t id; Point translation; std::optional<Rect> clip; };
		auto const Contains = [](Rect const& area, Point value) {
			return value.x >= area.position.x && value.y >= area.position.y &&
				value.x < area.position.x + area.size.width && value.y < area.position.y + area.size.height;
		};
		std::optional<HitTestResult> result;
		std::vector<Visit> pending{{0u, {}, std::nullopt}};
		while (!pending.empty()) {
			auto visit = pending.back();
			pending.pop_back();
			auto const& node = nodes[visit.id];
			if (auto const* transform = std::get_if<TransformVisual>(&node.visual)) {
				visit.translation.x += transform->transform.translation.x;
				visit.translation.y += transform->transform.translation.y;
			} else if (auto const* clip = std::get_if<ClipVisual>(&node.visual)) {
				auto bounds = clip->bounds;
				bounds.position.x += visit.translation.x;
				bounds.position.y += visit.translation.y;
				if (visit.clip) {
					auto const left = std::max(visit.clip->position.x, bounds.position.x);
					auto const top = std::max(visit.clip->position.y, bounds.position.y);
					auto const right = std::min(visit.clip->position.x + visit.clip->size.width, bounds.position.x + bounds.size.width);
					auto const bottom = std::min(visit.clip->position.y + visit.clip->size.height, bounds.position.y + bounds.size.height);
					visit.clip = Rect{{left, top}, {std::max(0.0f, right - left), std::max(0.0f, bottom - top)}};
				} else visit.clip = bounds;
			} else {
				auto role = HitTestRole::Content;
				auto region = WindowResizeRegion::None;
				MenuPath path;
				auto bounds = std::visit([&](auto const& state) {
					using State = std::remove_cvref_t<decltype(state)>;
					if constexpr (std::same_as<State, RectangleVisual> || std::same_as<State, GradientRectangleVisual>) { role = state.hit_test_role; return state.bounds; }
					else if constexpr (std::same_as<State, TextVisual>) return state.bounds;
					else if constexpr (std::same_as<State, HitTestVisual>) { role = state.role; region = state.resize_region; path = state.menu_path; return state.bounds; }
					else return Rect{};
				}, node.visual);
				bounds.position.x += visit.translation.x;
				bounds.position.y += visit.translation.y;
				if (Contains(bounds, point) && (!visit.clip || Contains(*visit.clip, point)) &&
					(!result || result->logical_id != node.logical_id || role != HitTestRole::Content)) {
					result = HitTestResult{node.logical_id, {point.x - bounds.position.x, point.y - bounds.position.y}, bounds.size, role, region, std::move(path)};
				}
			}
			std::vector<std::uint64_t> children;
			for (auto child = node.child; child != 0u; child = nodes[child].sibling) children.emplace_back(child);
			for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator) pending.emplace_back(Visit{*iterator, visit.translation, visit.clip});
		}
		return result;
	}
}



