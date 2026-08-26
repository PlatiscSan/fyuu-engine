module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <optional>
#include <format>
#endif
module fyuu_ui:emit_numeric_box_impl;
import :emitter;
#if defined(__cpp_lib_modules)
import std;
#endif
namespace fyuu_ui::detail {
	// NumericBox emits input chrome followed by formatted text. Formatting precision
	// is presentation state and does not participate in the fixed editor measurement.
	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, NumericBox const& value) {
		EmitterOutput output;
		auto const style = ResolveStyle(
		    theme.input,
		    local_style,
		    foreground,
		    font_size,
		    value.interaction,
		    !value.read_only,
		    false,
		    value.focused
		);
		output.Append(RectangleVisual{bounds, style.visual.background});
		output.Append(
		    TextVisual{
		        PaddedTextBounds(bounds, theme.horizontal_padding),
		        std::format("{:.{}f}", value.value, value.decimal_places),
		        style.visual.foreground,
		        style.font_size,
		        std::nullopt
		    }
		);
		return output;
	}
} // namespace fyuu_ui::detail
