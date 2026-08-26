module fyuu_ui:emit_border_impl;
import :emitter;
namespace fyuu_ui::detail {
	// Border contributes only its background primitive. Logical children attach to
	// the node's transform anchor and are painted after this rectangle.
	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, Border const& value) {
		EmitterOutput output;
		output.Append(
		    RectangleVisual{bounds, local_style.background.value_or(value.background)}
		);
		return output;
	}
} // namespace fyuu_ui::detail
