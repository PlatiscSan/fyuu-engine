module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <vector>
#include <algorithm>
#include <string>
#include <cstdint>
#include <optional>
#endif
module fyuu_ui:emit_window_impl;
#if defined(__cpp_lib_modules)
import std;
#endif
import :emitter;

namespace fyuu_ui::detail {
	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, Window const& widget) {
		EmitterOutput output;
		// Paint back-to-front: client surface, title chrome, close affordance,
		// frame, then invisible resize hit regions. The ordering is significant
		// because later visuals win hit testing.
		// Logical children are arranged only inside the client area; non-client
		// visuals stay attached to the window's own transform.
		auto const client_bounds = WindowClientBounds(bounds, theme);
		output.Append(
		    RectangleVisual{
		        client_bounds,
		        local_style.background.value_or(theme.window_client)
		    }
		);
		auto title_bounds = bounds;
		title_bounds.size.height =
		    std::min(theme.window_title_height, bounds.size.height);
		output.Append(
		    GradientRectangleVisual{
		        title_bounds,
		        widget.active ? theme.window_non_client_glass :
		                        theme.window_non_client_inactive_glass,
		        widget.active ? theme.window_non_client :
		                        theme.window_non_client_inactive,
		        HitTestRole::WindowNonClient
		    }
		);
		output.Append(
		    RectangleVisual{
		        {title_bounds.position, {title_bounds.size.width, 1.0f}},
		        theme.window_non_client_highlight,
		        HitTestRole::WindowNonClient
		    }
		);
		title_bounds.position.x += theme.window_horizontal_padding;
		title_bounds.size.width = std::max(
		    0.0f,
		    title_bounds.size.width - theme.window_horizontal_padding * 2.0f -
		        theme.window_non_client_button_width
		);
		output.Append(
		    TextVisual{
		        title_bounds,
		        widget.title,
		        foreground.value_or(
		            widget.active ? theme.window_title : theme.window_title_inactive
		        ),
		        font_size.value_or(theme.window_font_size),
		        std::nullopt
		    }
		);
		if (widget.closable) {
			// Keep the close hit target at full title-button size while insetting only
			// its painted face. This preserves an easy target at the window edge.
			auto close_bounds = title_bounds;
			close_bounds.position.x = std::max(
			    0.0f,
			    bounds.size.width - theme.window_non_client_button_width
			);
			close_bounds.size.width =
			    std::min(theme.window_non_client_button_width, bounds.size.width);
			auto button_bounds = close_bounds;
			button_bounds.position.x += 3.0f;
			button_bounds.position.y += 5.0f;
			button_bounds.size.width = std::max(0.0f, button_bounds.size.width - 8.0f);
			button_bounds.size.height = std::max(0.0f, button_bounds.size.height - 10.0f);
			auto button_top = theme.window_non_client_button_highlight;
			auto button_bottom = theme.window_non_client_button;
			switch (widget.non_client_button_interaction) {
				case InteractionState::Hovered:
					button_top = theme.window_non_client_button_hovered_highlight;
					button_bottom = theme.window_non_client_button_hovered;
					break;
				case InteractionState::Pressed:
					button_top = theme.window_non_client_button_pressed_highlight;
					button_bottom = theme.window_non_client_button_pressed;
					break;
				case InteractionState::Normal:
					break;
			}
			output.Append(
			    GradientRectangleVisual{
			        button_bounds,
			        button_top,
			        button_bottom,
			        HitTestRole::WindowNonClientButton
			    }
			);
			auto const close_center = Point{
			    button_bounds.position.x + button_bounds.size.width * 0.5f,
			    button_bounds.position.y + button_bounds.size.height * 0.5f
			};
			constexpr auto close_radius = 4.0f;
			output.Append(
			    LineVisual{
			        {close_center.x - close_radius, close_center.y - close_radius},
			        {close_center.x + close_radius, close_center.y + close_radius},
			        theme.window_non_client_button_foreground,
			        1.25f
			    }
			);
			output.Append(
			    LineVisual{
			        {close_center.x + close_radius, close_center.y - close_radius},
			        {close_center.x - close_radius, close_center.y + close_radius},
			        theme.window_non_client_button_foreground,
			        1.25f
			    }
			);
		}
		output.Append(
		    RectangleVisual{
		        {{0.0f, std::max(0.0f, theme.window_title_height - 1.0f)},
		            {bounds.size.width, 1.0f}},
		        theme.window_non_client_shadow
		    }
		);
		output.Append(
		    RectangleVisual{{{}, {bounds.size.width, 1.0f}}, theme.window_border}
		);
		output.Append(
		    RectangleVisual{
		        {{0.0f, std::max(0.0f, bounds.size.height - 1.0f)},
		            {bounds.size.width, 1.0f}},
		        theme.window_border
		    }
		);
		output.Append(
		    RectangleVisual{{{}, {1.0f, bounds.size.height}}, theme.window_border}
		);
		output.Append(
		    RectangleVisual{
		        {{std::max(0.0f, bounds.size.width - 1.0f), 0.0f},
		            {1.0f, bounds.size.height}},
		        theme.window_border
		    }
		);
		if (widget.resizable) {
			// Edge regions are emitted before corner regions. Overlapping corners are
			// therefore topmost and select diagonal resizing deterministically.
			auto const resize_border = std::min(
			    theme.window_resize_border,
			    std::min(bounds.size.width, bounds.size.height) * 0.5f
			);
			auto const corner_size = resize_border * 2.0f;
			output.Append(
			    HitTestVisual{
			        {{}, {resize_border, bounds.size.height}},
			        HitTestRole::WindowResize,
			        WindowResizeRegion::Left
			    }
			);
			output.Append(
			    HitTestVisual{
			        {{}, {bounds.size.width, resize_border}},
			        HitTestRole::WindowResize,
			        WindowResizeRegion::Top
			    }
			);
			output.Append(
			    HitTestVisual{
			        {{std::max(0.0f, bounds.size.width - resize_border), 0.0f},
			            {resize_border, bounds.size.height}},
			        HitTestRole::WindowResize,
			        WindowResizeRegion::Right
			    }
			);
			output.Append(
			    HitTestVisual{
			        {{0.0f, std::max(0.0f, bounds.size.height - resize_border)},
			            {bounds.size.width, resize_border}},
			        HitTestRole::WindowResize,
			        WindowResizeRegion::Bottom
			    }
			);
			output.Append(
			    HitTestVisual{
			        {{}, {corner_size, corner_size}},
			        HitTestRole::WindowResize,
			        WindowResizeRegion::TopLeft
			    }
			);
			output.Append(
			    HitTestVisual{
			        {{std::max(0.0f, bounds.size.width - corner_size), 0.0f},
			            {corner_size, corner_size}},
			        HitTestRole::WindowResize,
			        WindowResizeRegion::TopRight
			    }
			);
			output.Append(
			    HitTestVisual{
			        {{0.0f, std::max(0.0f, bounds.size.height - corner_size)},
			            {corner_size, corner_size}},
			        HitTestRole::WindowResize,
			        WindowResizeRegion::BottomLeft
			    }
			);
			output.Append(
			    HitTestVisual{
			        {{std::max(0.0f, bounds.size.width - corner_size),
			             std::max(0.0f, bounds.size.height - corner_size)},
			            {corner_size, corner_size}},
			        HitTestRole::WindowResize,
			        WindowResizeRegion::BottomRight
			    }
			);
		}
		return output;
	}
} // namespace fyuu_ui::detail
