module;
#include <algorithm>
module fyuu_ui:emit_containers_impl;
import :emitter;
#if defined(__cpp_lib_modules)
import std;
#endif

namespace fyuu_ui::detail {
	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, Overlay const& value) {
		EmitterOutput output;
		// A clip becomes the parent anchor so every subsequently emitted child is
		// clipped as a group, rather than clipping only the overlay's own visuals.
		if (value.clip_to_bounds)
			output.SetAnchor(output.Append(ClipVisual{bounds}));
		return output;
	}
	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, WindowLayer const&) {
		EmitterOutput output;
		// WindowLayer controls layout and z-order; it has no visual of its own.
		return output;
	}
	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, StackPanel const&) {
		EmitterOutput output;
		// StackPanel contributes geometry during layout, not during visual emission.
		return output;
	}
	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, ScrollView const& value) {
		EmitterOutput output;
		// The clip becomes the child anchor; the arranged child transform supplies
		// the scroll offset while every overflowing visual remains inside viewport.
		auto const clip = output.Append(ClipVisual{bounds});
		output.SetAnchor(clip);
		if (first_child_size.height <= bounds.size.height ||
		    bounds.size.height <= 0.0f) {
			return output;
		}

		// The thumb represents the visible fraction of the child. It is emitted
		// after the clip but before child traversal, so it stays inside the viewport.
		constexpr auto track_width = 8.0f;
		constexpr auto inset = 2.0f;
		auto const track_height = std::max(0.0f, bounds.size.height - inset * 2.0f);
		auto const thumb_height = std::min(
		    track_height,
		    std::max(
		        24.0f,
		        track_height * bounds.size.height / first_child_size.height
		    )
		);
		auto const maximum_offset = first_child_size.height - bounds.size.height;
		auto const offset = std::clamp(value.offset, 0.0f, maximum_offset);
		auto const thumb_travel = std::max(0.0f, track_height - thumb_height);
		auto const thumb_y = inset + thumb_travel * offset / maximum_offset;
		auto const track = Rect{
		    {bounds.size.width - track_width - inset, inset},
		    {track_width, track_height}
		};
		output.Append(RectangleVisual{track, theme.surface});
		auto thumb = track;
		thumb.position.y = thumb_y;
		thumb.size.height = thumb_height;
		Color thumb_color;
		switch (value.vertical_scroll_bar.interaction) {
			case InteractionState::Normal:
				thumb_color = theme.slider_thumb.normal.background;
				break;
			case InteractionState::Hovered:
				thumb_color = theme.slider_thumb.hovered.background;
				break;
			case InteractionState::Pressed:
				thumb_color = theme.slider_thumb.pressed.background;
				break;
		}
		output.Append(RectangleVisual{thumb, thumb_color});
		return output;
	}
	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, SplitView const& value) {
		EmitterOutput output;
		// A divider is meaningful only when it separates two children and can be
		// dragged. Fixed or incomplete splits intentionally emit no decoration.
		if (!value.resizable || value.spacing <= 0.0f || !has_two_children)
			return output;
		// Reproduce the constrained first-pane extent used by arrangement so the
		// painted divider and the interactive split boundary remain coincident.
		auto divider = bounds;
		auto const extent = value.orientation == Orientation::Horizontal ?
		    bounds.size.width :
		    bounds.size.height;
		auto const available = std::max(0.0f, extent - value.spacing);
		auto const maximum_first =
		    std::max(0.0f, available - std::min(value.minimum_second, available));
		auto const first = std::clamp(
		    available * std::clamp(value.split, 0.0f, 1.0f),
		    std::min(value.minimum_first, maximum_first),
		    maximum_first
		);
		if (value.orientation == Orientation::Horizontal) {
			divider.position.x += first;
			divider.size.width = value.spacing;
		} else {
			divider.position.y += first;
			divider.size.height = value.spacing;
		}
		Color divider_color;
		switch (value.interaction) {
			case InteractionState::Normal:
				divider_color = theme.divider;
				break;
			case InteractionState::Hovered:
				divider_color = theme.slider_thumb.hovered.background;
				break;
			case InteractionState::Pressed:
				divider_color = theme.slider_thumb.pressed.background;
				break;
		}
		output.Append(RectangleVisual{divider, divider_color});
		return output;
	}
} // namespace fyuu_ui::detail
