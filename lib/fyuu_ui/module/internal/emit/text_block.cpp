module;
#include <optional>
module fyuu_ui:emit_text_block_impl;
import :emitter;
#if defined(__cpp_lib_modules)
import std;
#endif
namespace fyuu_ui::detail {
	// TextBlock uses inherited foreground/font values when present; these are the
	// same resolved values used during measurement, preventing layout/render drift.
	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, TextBlock const& value) {
		EmitterOutput output;
		output.Append(
		    TextVisual{
		        bounds,
		        value.text,
		        foreground.value_or(value.color),
		        font_size.value_or(value.font_size),
		        std::nullopt
		    }
		);
		return output;
	}
} // namespace fyuu_ui::detail
