module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <vector>
#include <cstdint>
#include <type_traits>
#include <variant>
#include <span>
#endif

module fyuu_ui:draw_list;
#if defined(__cpp_lib_modules)
import std;
#endif
import :visual_tree;

namespace fyuu_ui::detail {
	std::vector<DrawCommand> BuildDrawList(std::span<VisualNode const> nodes) {
		struct Visit { std::uint64_t id; bool exiting = false; };
		std::vector<DrawCommand> result;
		result.reserve(nodes.size() * 2u);
		std::vector<Visit> pending{{0u}};
		while (!pending.empty()) {
			auto const visit = pending.back();
			pending.pop_back();
			auto const& visual = nodes[visit.id].visual;
			if (visit.exiting) {
				if (std::holds_alternative<TransformVisual>(visual)) result.emplace_back(PopTransformCommand{});
				else if (std::holds_alternative<ClipVisual>(visual)) result.emplace_back(PopClipCommand{});
				continue;
			}
			std::visit([&result](auto const& state) {
				using State = std::remove_cvref_t<decltype(state)>;
				if constexpr (std::same_as<State, RectangleVisual>) result.emplace_back(DrawRectangleCommand{state.bounds, state.fill});
				else if constexpr (std::same_as<State, SceneTextureVisual>) result.emplace_back(DrawSceneTextureCommand{state.bounds, state.fallback});
				else if constexpr (std::same_as<State, GradientRectangleVisual>) result.emplace_back(DrawGradientRectangleCommand{state.bounds, state.top, state.bottom});
				else if constexpr (std::same_as<State, LineVisual>) result.emplace_back(DrawLineCommand{state.start, state.end, state.color, state.thickness});
				else if constexpr (std::same_as<State, TextVisual>) result.emplace_back(DrawTextCommand{state.bounds, state.text, state.color, state.font_size, state.caret_offset, state.selection_start, state.selection_end, state.horizontal_offset, state.selection_color});
				else if constexpr (std::same_as<State, ClipVisual>) result.emplace_back(PushClipCommand{state.bounds});
				else if constexpr (std::same_as<State, TransformVisual>) result.emplace_back(PushTransformCommand{state.transform});
			}, visual);
			if (std::holds_alternative<TransformVisual>(visual) || std::holds_alternative<ClipVisual>(visual)) pending.emplace_back(Visit{visit.id, true});
			std::vector<std::uint64_t> children;
			for (auto child = nodes[visit.id].child; child != 0u; child = nodes[child].sibling) children.emplace_back(child);
			for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator) pending.emplace_back(Visit{*iterator});
		}
		return result;
	}
}


