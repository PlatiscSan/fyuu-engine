module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#endif
export module fyuu_ui:container_scroll_view;
#if defined(__cpp_lib_modules)
import std;
#endif
import :control_common;
import :control_scroll_bar;
import :geometry;

export namespace fyuu_ui {
	/// Displays one vertically scrollable child through a clipped viewport.
	struct ScrollView {
		float offset = 0.0f;
		// Arrange updates these derived extents for pointer interaction. Building a
		// visual tree is therefore an explicit non-const LogicalTree operation.
		float viewport_extent = 0.0f;
		float content_extent = 0.0f;
		ScrollBar vertical_scroll_bar;
	};

	/// Applies one host wheel delta; layout clamps the result to the current extent.
	inline void Scroll(ScrollView& view, float delta) noexcept {
		view.offset = std::max(0.0f, view.offset - delta * 36.0f);
	}

	inline bool DragScrollBar(ScrollView& view, float pointer) noexcept {
		auto& bar = view.vertical_scroll_bar;
		if (!bar.dragging)
			return false;
		auto const track = std::max(0.0f, view.viewport_extent - 4.0f);
		auto const thumb =
		    std::min(track, std::max(24.0f, track * view.viewport_extent / view.content_extent));
		auto const travel = track - thumb;
		if (travel > 0.0f) {
			auto const top = std::clamp(pointer - bar.track_origin - bar.grab_offset, 0.0f, travel);
			view.offset = top / travel * (view.content_extent - view.viewport_extent);
		}
		return true;
	}

	inline bool BeginScrollBarDrag(
	    ScrollView& view,
	    Point pointer,
	    Point hit_position,
	    Size hit_size
	) noexcept {
		if (view.content_extent <= view.viewport_extent || view.viewport_extent <= 0.0f)
			return false;
		constexpr auto inset = 2.0f;
		auto const track = std::max(0.0f, view.viewport_extent - inset * 2.0f);
		auto const thumb =
		    std::min(track, std::max(24.0f, track * view.viewport_extent / view.content_extent));
		auto const maximum = view.content_extent - view.viewport_extent;
		auto const travel = track - thumb;
		auto const ratio = std::clamp(view.offset, 0.0f, maximum) / maximum;
		auto& bar = view.vertical_scroll_bar;
		bar.dragging = true;
		bar.interaction = InteractionState::Pressed;
		if (hit_size.height < track) {
			bar.track_origin = pointer.y - hit_position.y - travel * ratio;
			bar.grab_offset = hit_position.y;
		} else {
			bar.track_origin = pointer.y - hit_position.y;
			bar.grab_offset = thumb * 0.5f;
			DragScrollBar(view, pointer.y);
		}
		return true;
	}

	inline bool EndScrollBarDrag(ScrollView& view) noexcept {
		auto& bar = view.vertical_scroll_bar;
		auto const dragged = bar.dragging;
		bar.dragging = false;
		bar.interaction = InteractionState::Normal;
		return dragged;
	}
	inline void SetScrollBarInteraction(ScrollView& view, InteractionState state) noexcept {
		if (!view.vertical_scroll_bar.dragging)
			view.vertical_scroll_bar.interaction = state;
	}

} // namespace fyuu_ui
