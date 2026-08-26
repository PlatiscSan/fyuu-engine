module;
#include <optional>
module fyuu_ui:emit_toggle_button_impl;
import :emitter;
namespace fyuu_ui::detail {
	// ToggleButton shares Button chrome, but checked—not default-button status—is
	// the persistent selected state passed to style resolution.
	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, ToggleButton const& value) {
		EmitterOutput output;
		auto const style = ResolveStyle(
		    theme.button,
		    local_style,
		    foreground,
		    font_size,
		    value.interaction,
		    value.enabled,
		    value.checked,
		    false
		);
		output.Append(RectangleVisual{bounds, style.visual.background});
		output.Append(
		    TextVisual{
		        bounds,
		        value.title,
		        style.visual.foreground,
		        style.font_size,
		        std::nullopt
		    }
		);
		return output;
	}
} // namespace fyuu_ui::detail
