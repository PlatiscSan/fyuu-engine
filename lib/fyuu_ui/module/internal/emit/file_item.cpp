module;
#include <algorithm>
#include <optional>
module fyuu_ui:emit_file_item_impl;
import :emitter;
namespace fyuu_ui::detail {
	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, FileItem const& value) {
		EmitterOutput output;
		auto const style = ResolveStyle(
		    theme.menu_item,
		    local_style,
		    foreground,
		    font_size,
		    value.interaction,
		    value.enabled,
		    value.selected,
		    false
		);
		// The row remains visually flat in its normal state. Hover and selection
		// come from menu_item, which already carries the subdued surface palette.
		output.Append(RectangleVisual{bounds, style.visual.background});
		auto icon = Rect{
		    {bounds.position.x + 8.0f, bounds.position.y + 8.0f},
		    {15.0f, 12.0f}
		};
		if (value.directory) {
			// Two rectangles form a small folder tab without adding an image asset.
			output.Append(
			    RectangleVisual{
			        {icon.position, {7.0f, 3.0f}},
			        theme.window_client_muted_text
			    }
			);
			icon.position.y += 3.0f;
			icon.size.height -= 3.0f;
			output.Append(RectangleVisual{icon, theme.window_client_text});
		} else {
			output.Append(RectangleVisual{icon, theme.window_client_muted_text});
			output.Append(
			    LineVisual{
			        {icon.position.x + 4.0f, icon.position.y + 4.0f},
			        {icon.position.x + 11.0f, icon.position.y + 4.0f},
			        theme.window_client,
			        1.0f
			    }
			);
		}
		auto text_bounds = bounds;
		text_bounds.position.x += 31.0f;
		text_bounds.size.width = std::max(0.0f, text_bounds.size.width - 39.0f);
		output.Append(
		    TextVisual{
		        text_bounds,
		        value.title,
		        style.visual.foreground,
		        style.font_size,
		        std::nullopt
		    }
		);
		return output;
	}
} // namespace fyuu_ui::detail
