module;
#include <algorithm>
#include <optional>
module fyuu_ui:emit_check_box_impl;
import :emitter;
#if defined(__cpp_lib_modules)
import std;
#endif
namespace fyuu_ui::detail {
	// The indicator occupies a fixed theme-sized leading slot. Text begins after
	// indicator_spacing and receives the remainder of the arranged bounds.
	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, CheckBox const& value) {
		EmitterOutput output;
		Rect indicator{
		    {
				bounds.position.x,
		        bounds.position.y + (bounds.size.height - theme.indicator_size) * 0.5f
			},
		    {
				theme.indicator_size, 
				theme.indicator_size
			}
		};
		auto const style = ResolveStyle(
		    theme.indicator,
		    local_style,
		    foreground,
		    font_size,
		    value.interaction,
		    value.enabled,
		    value.checked,
		    false
		);
		output.Append(RectangleVisual{indicator, style.visual.background});
		output.Append(
		    TextVisual{
		        {
					{ 
						bounds.position.x + theme.indicator_size + theme.indicator_spacing, 
						bounds.position.y 
					},
		            {
						std::max(0.0f, bounds.size.width - theme.indicator_size - theme.indicator_spacing), 
						bounds.size.height
					}
				},
		        value.title,
		        style.visual.foreground,
		        style.font_size,
		        std::nullopt
		    }
		);
		return output;
	}
} // namespace fyuu_ui::detail
