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
module fyuu_ui:emit_menu_bar_impl;
#if defined(__cpp_lib_modules)
import std;
#endif
import :emitter;

namespace fyuu_ui::detail {
	namespace {
		// Pressed takes precedence when pointer capture remains active while the
		// pointer is also hovering the same menu item.
		InteractionState ResolveInteraction(bool pressed, bool hovered) noexcept {
			auto const state =
			    static_cast<unsigned>(pressed) << 1u | static_cast<unsigned>(hovered);
			switch (state) {
				case 2u:
				case 3u:
					return InteractionState::Pressed;
				case 1u:
					return InteractionState::Hovered;
				case 0u:
					return InteractionState::Normal;
			}
			return InteractionState::Normal;
		}
	} // namespace

	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, MenuBar const& widget) {
		EmitterOutput output;
		// MenuPath stores root-to-leaf entry indices. Event routing consumes this
		// exact representation, so hit visuals must use the same level ordering.
		auto const item_font = font_size.value_or(theme.menu_item.font_size);
		auto const bar_height = bounds.size.height;
		// Bar background.
		output.Append(RectangleVisual{bounds, theme.panel});
		// Optional brand label.
		auto cursor_x = 0.0f;
		if (!widget.title.empty()) {
			auto const text = measure_text(widget.title, 14.0f);
			output.Append(
			    TextVisual{
			        Rect{{8.0f, 0.0f}, {text.width, bar_height}},
			        widget.title,
			        theme.text,
			        14.0f,
			        std::nullopt
			    }
			);
			cursor_x = text.width + 16.0f;
		}
		// Bar items; record each item's left offset for popup
		// placement.
		std::vector<float> bar_rects;
		bar_rects.reserve(widget.entries.size());
		for (std::size_t i = 0u; i < widget.entries.size(); ++i) {
			auto const& entry = widget.entries[i];
			auto const width = measure_text(entry.title, item_font).width + 16.0f;
			auto const item_rect = Rect{{cursor_x, 0.0f}, {width, bar_height}};
			bar_rects.push_back(cursor_x);
			cursor_x += width;
			auto const is_open = !widget.open_path.empty() && widget.open_path[0u] == i;
			auto const is_hover = widget.hover_path.size() == 1u && widget.hover_path[0u] == i;
			auto const is_pressed =
			    widget.pressed_path.size() == 1u && widget.pressed_path[0u] == i;
			auto const interaction = ResolveInteraction(is_pressed, is_hover);
			auto const style = ResolveStyle(
			    theme.menu_item,
			    local_style,
			    foreground,
			    font_size,
			    interaction,
			    true,
			    is_open,
			    false
			);
			output.Append(RectangleVisual{item_rect, style.visual.background});
			output.Append(
			    TextVisual{
			        Rect{
			            {item_rect.position.x + 6.0f, item_rect.position.y},
			            {std::max(0.0f, item_rect.size.width - 12.0f), item_rect.size.height}
			        },
			        entry.title,
			        style.visual.foreground,
			        style.font_size,
			        std::nullopt
			    }
			);
			output.Append(
			    HitTestVisual{
			        item_rect,
			        fyuu_ui::HitTestRole::MenuContent,
			        fyuu_ui::WindowResizeRegion::None,
			        fyuu_ui::MenuPath{{static_cast<std::uint32_t>(i)}}
			    }
			);
		}
		// Emit popups after the bar so they remain topmost in both painting and hit
		// testing. menu_entries points into widget.entries; emission never mutates
		// that storage, so advancing the view through nested children is safe.
		if (!widget.open_path.empty()) {
			auto const& open = widget.open_path;
			if (!open.empty() && open[0u] < widget.entries.size() &&
			    !widget.entries[open[0u]].children.empty()) {
				auto const* menu_entries = widget.entries[open[0u]].children.data();
				auto menu_count = widget.entries[open[0u]].children.size();
				auto panel_origin = fyuu_ui::Point{bar_rects[open[0u]], bar_height + 8.0f};
				for (std::size_t level = 0u; level < open.size(); ++level) {
					auto panel_width = 180.0f;
					for (std::size_t i = 0u; i < menu_count; ++i) {
						panel_width = std::max(
						    panel_width,
						    measure_text(menu_entries[i].title, item_font).width + 16.0f
						);
					}
					auto const panel = fyuu_ui::Rect{
					    panel_origin,
					    {panel_width, static_cast<float>(menu_count) * 22.0f}
					};
					output.Append(RectangleVisual{panel, theme.raised_surface});
					for (std::size_t i = 0u; i < menu_count; ++i) {
						auto const item_rect = fyuu_ui::Rect{
						    {panel_origin.x, panel_origin.y + static_cast<float>(i) * 22.0f},
						    {panel_width, 22.0f}
						};
						std::vector<std::uint32_t> path(open.begin(), open.begin() + level + 1);
						path.push_back(static_cast<std::uint32_t>(i));
						auto const is_hover = widget.hover_path == path;
						auto const is_pressed = widget.pressed_path == path;
						auto const selected = (level + 1u < open.size() && open[level + 1u] == i) ||
						    menu_entries[i].checked;
						auto const interaction = ResolveInteraction(is_pressed, is_hover);
						auto const style = ResolveStyle(
						    theme.menu_item,
						    local_style,
						    foreground,
						    font_size,
						    interaction,
						    menu_entries[i].enabled,
						    selected,
						    false
						);
						output.Append(RectangleVisual{item_rect, style.visual.background});
						output.Append(
						    TextVisual{
						        Rect{
						            {item_rect.position.x + 6.0f, item_rect.position.y},
						            {std::max(0.0f, item_rect.size.width - 12.0f),
						                item_rect.size.height}
						        },
						        menu_entries[i].title,
						        style.visual.foreground,
						        style.font_size,
						        std::nullopt
						    }
						);
						if (!menu_entries[i].children.empty()) {
							output.Append(
							    TextVisual{
							        Rect{
							            {item_rect.position.x + item_rect.size.width - 18.0f,
							                item_rect.position.y},
							            {14.0f, item_rect.size.height}
							        },
							        "\u203A",
							        theme.muted_text,
							        style.font_size,
							        std::nullopt
							    }
							);
						}
						output.Append(
						    HitTestVisual{
						        item_rect,
						        fyuu_ui::HitTestRole::MenuContent,
						        fyuu_ui::WindowResizeRegion::None,
						        fyuu_ui::MenuPath{std::move(path)}
						    }
						);
					}
					if (level + 1u < open.size()) {
						auto const parent_index = open[level + 1u];
						if (parent_index < menu_count) {
							auto const& parent_item = menu_entries[parent_index];
							panel_origin = fyuu_ui::Point{
							    panel.position.x + panel.size.width,
							    panel.position.y + static_cast<float>(parent_index) * 22.0f
							};
							menu_entries = parent_item.children.data();
							menu_count = parent_item.children.size();
						}
					}
				}
			}
		}
		return output;
	}
} // namespace fyuu_ui::detail
