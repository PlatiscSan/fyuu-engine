module;
#include <cstddef>
#include <algorithm>
#include <optional>
module fyuu_ui:emit_text_box_impl;
import :emitter;
#if defined(__cpp_lib_modules)
import std;
#endif
namespace fyuu_ui::detail {
	// Placeholder color/text are used only while empty and unfocused. A caret offset
	// is emitted only for the focused editor and is clamped to the current text.
	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, TextBox const& value) {
		EmitterOutput output;
		auto const style = ResolveStyle(
		    theme.input,
		    local_style,
		    foreground,
		    font_size,
		    InteractionState::Normal,
		    true,
		    false,
		    value.focused
		);
		output.Append(RectangleVisual{bounds, style.visual.background});
		auto const show_placeholder = value.text.empty() && !value.focused;
		auto const caret = value.focused ?
		    std::optional<std::size_t>{(std::min)(value.caret_offset, value.text.size())} :
		    std::nullopt;
		auto const [selection_start, selection_end] = TextSelection(value);
		auto const has_selection = value.focused && selection_start != selection_end;
		output.Append(
		    TextVisual{
		        PaddedTextBounds(bounds, theme.horizontal_padding),
		        show_placeholder ? value.placeholder : value.text,
		        show_placeholder ? theme.muted_text : style.visual.foreground,
		        style.font_size,
		        caret,
		        has_selection ? std::optional<std::size_t>{selection_start} : std::nullopt,
		        has_selection ? std::optional<std::size_t>{selection_end} : std::nullopt,
		        value.horizontal_offset,
		        theme.input.selected.background
		    }
		);
		return output;
	}
} // namespace fyuu_ui::detail
