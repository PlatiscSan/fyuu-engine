module;
#include <algorithm>
module fyuu_ui:emit_slider_impl;
import :emitter;
#if defined(__cpp_lib_modules)
import std;
#endif
namespace fyuu_ui::detail {
	// The transparent full-bounds rectangle preserves hit geometry. Track and thumb
	// are presentation children; value is normalized only when the range is valid.
	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, Slider const& value) {
		EmitterOutput output;
		output.Append(RectangleVisual{bounds, {0.0f, 0.0f, 0.0f, 0.0f}});
		auto const range = value.maximum - value.minimum;
		auto const ratio =
		    range > 0.0f ? std::clamp((value.value - value.minimum) / range, 0.0f, 1.0f) : 0.0f;
		auto track = bounds;
		auto thumb = bounds;
		if (value.orientation == Orientation::Horizontal) {
			track.position.y += (track.size.height - theme.slider_track_thickness) * 0.5f;
			track.size.height = theme.slider_track_thickness;
			thumb.position.x += (thumb.size.width - theme.slider_thumb_size) * ratio;
			thumb.position.y += (thumb.size.height - theme.slider_thumb_size) * 0.5f;
		} else {
			track.position.x += (track.size.width - theme.slider_track_thickness) * 0.5f;
			track.size.width = theme.slider_track_thickness;
			thumb.position.x += (thumb.size.width - theme.slider_thumb_size) * 0.5f;
			thumb.position.y +=
			    (thumb.size.height - theme.slider_thumb_size) * (1.0f - ratio);
		}
		thumb.size = {theme.slider_thumb_size, theme.slider_thumb_size};
		output.Append(RectangleVisual{track, theme.slider_track});
		auto const style = ResolveStyle(
		    theme.slider_thumb,
		    local_style,
		    foreground,
		    font_size,
		    value.interaction,
		    true,
		    true,
		    false
		);
		output.Append(RectangleVisual{thumb, style.visual.background});
		return output;
	}
} // namespace fyuu_ui::detail
