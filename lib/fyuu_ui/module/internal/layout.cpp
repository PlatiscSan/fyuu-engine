module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <string>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <optional>
#include <variant>
#include <span>
#include <format>
#endif // !defined(__cpp_lib_modules)

module fyuu_ui:layout_impl;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :logical_tree;
import :theme;

namespace {

	fyuu_ui::VisualStyle const& ResolveStyle(
		fyuu_ui::WidgetStyle const& style,
		fyuu_ui::InteractionState interaction,
		bool enabled,
		bool selected,
		bool focused
	) noexcept {
		if (!enabled) {
			return style.disabled;
		}
		if (interaction == fyuu_ui::InteractionState::Pressed) {
			return style.pressed;
		}
		if (interaction == fyuu_ui::InteractionState::Hovered) {
			return selected ? style.selected_hovered : style.hovered;
		}
		if (selected) {
			return style.selected;
		}
		if (focused) {
			return style.focused;
		}
		return style.normal;
	}

	struct ResolvedWidgetStyle {
		fyuu_ui::VisualStyle visual;
		float font_size;
	};

	ResolvedWidgetStyle ResolveStyle(
		fyuu_ui::WidgetStyle const& style,
		fyuu_ui::StyleOverride const& local,
		std::optional<fyuu_ui::Color> inherited_foreground,
		std::optional<float> inherited_font_size,
		fyuu_ui::InteractionState interaction,
		bool enabled,
		bool selected,
		bool focused
	) noexcept {
		auto visual = ResolveStyle(style, interaction, enabled, selected, focused);
		visual.background = local.background.value_or(visual.background);
		visual.foreground = inherited_foreground.value_or(visual.foreground);
		return {
			visual,
			inherited_font_size.value_or(style.font_size)
		};
	}

	fyuu_ui::Size MaximumSize(std::span<fyuu_ui::Size const> sizes) {
		fyuu_ui::Size result;
		for (auto const& size : sizes) {
			result.width = std::max(result.width, size.width);
			result.height = std::max(result.height, size.height);
		}
		return result;
	}

	fyuu_ui::Size MeasureText(std::string const& text, float font_size) {
		return fyuu_ui::Size{
			static_cast<float>(text.size()) * font_size * 0.6f,
			font_size * 1.2f
		};
	}

	float ClampDimension(
		float desired,
		std::optional<float> explicit_size,
		float minimum,
		std::optional<float> maximum
	) {
		auto result = explicit_size.value_or(desired);
		result = std::max(result, minimum);
		if (maximum.has_value()) {
			result = std::min(result, *maximum);
		}
		return std::max(result, 0.0f);
	}

	fyuu_ui::Size ApplyMeasureProperties(
		fyuu_ui::Size const& desired,
		fyuu_ui::LayoutProperties const& properties
	) {
		return fyuu_ui::Size{
			ClampDimension(
				desired.width,
				properties.width,
				properties.minimum_width,
				properties.maximum_width
			) + properties.margin.left + properties.margin.right,
			ClampDimension(
				desired.height,
				properties.height,
				properties.minimum_height,
				properties.maximum_height
			) + properties.margin.top + properties.margin.bottom
		};
	}

	fyuu_ui::Rect ApplyArrangeProperties(
		fyuu_ui::Rect const& slot,
		fyuu_ui::Size const& desired,
		fyuu_ui::LayoutProperties const& properties
	) {
		auto const available_width = std::max(
			0.0f,
			slot.size.width - properties.margin.left - properties.margin.right
		);
		auto const available_height = std::max(
			0.0f,
			slot.size.height - properties.margin.top - properties.margin.bottom
		);
		auto const desired_width = std::max(
			0.0f,
			desired.width - properties.margin.left - properties.margin.right
		);
		auto const desired_height = std::max(
			0.0f,
			desired.height - properties.margin.top - properties.margin.bottom
		);
		auto width = properties.horizontal_alignment == fyuu_ui::Alignment::Stretch &&
			!properties.width.has_value() ? available_width : std::min(desired_width, available_width);
		auto height = properties.vertical_alignment == fyuu_ui::Alignment::Stretch &&
			!properties.height.has_value() ? available_height : std::min(desired_height, available_height);
		auto x = slot.position.x + properties.margin.left;
		auto y = slot.position.y + properties.margin.top;
		if (properties.horizontal_alignment == fyuu_ui::Alignment::Center) {
			x += (available_width - width) * 0.5f;
		}
		else if (properties.horizontal_alignment == fyuu_ui::Alignment::End) {
			x += available_width - width;
		}
		if (properties.vertical_alignment == fyuu_ui::Alignment::Center) {
			y += (available_height - height) * 0.5f;
		}
		else if (properties.vertical_alignment == fyuu_ui::Alignment::End) {
			y += available_height - height;
		}
		return fyuu_ui::Rect{ { x, y }, { width, height } };
	}

	fyuu_ui::Size MeasureWidget(
		fyuu_ui::Widget const& widget,
		std::optional<float> inherited_font_size
	) {
		return std::visit(
			[inherited_font_size](auto const& state) {
				using State = std::remove_cvref_t<decltype(state)>;
			if constexpr (std::same_as<State, fyuu_ui::Spacer>) {
				return state.size;
			}
			else if constexpr (std::same_as<State, fyuu_ui::TextBlock>) {
				return MeasureText(state.text, inherited_font_size.value_or(state.font_size));
			}
			else if constexpr (std::same_as<State, fyuu_ui::Separator>) {
				if (state.orientation == fyuu_ui::Orientation::Horizontal) {
					return fyuu_ui::Size{ 0.0f, state.thickness };
				}
				return fyuu_ui::Size{ state.thickness, 0.0f };
			}
			else if constexpr (std::same_as<State, fyuu_ui::ProgressBar>) {
				return fyuu_ui::Size{ 120.0f, 16.0f };
			}
			else if constexpr (
				std::same_as<State, fyuu_ui::Button> ||
				std::same_as<State, fyuu_ui::ToggleButton> ||
				std::same_as<State, fyuu_ui::CheckBox> ||
				std::same_as<State, fyuu_ui::RadioButton>
			) {
				auto const text = MeasureText(state.title, inherited_font_size.value_or(14.0f));
				return fyuu_ui::Size{ text.width + 20.0f, std::max(text.height + 10.0f, 28.0f) };
			}
			else if constexpr (std::same_as<State, fyuu_ui::Slider>) {
				if (state.orientation == fyuu_ui::Orientation::Horizontal) {
					return fyuu_ui::Size{ 120.0f, 20.0f };
				}
				return fyuu_ui::Size{ 20.0f, 120.0f };
			}
			else if constexpr (
				std::same_as<State, fyuu_ui::TextBox> ||
				std::same_as<State, fyuu_ui::SearchBox> ||
				std::same_as<State, fyuu_ui::NumericBox>
			) {
				return fyuu_ui::Size{ 160.0f, 28.0f };
			}
			else if constexpr (std::same_as<State, fyuu_ui::MenuItem>) {
				auto const text = MeasureText(state.title, inherited_font_size.value_or(13.0f));
				return fyuu_ui::Size{ text.width + 16.0f, 22.0f };
			}
			else if constexpr (std::same_as<State, fyuu_ui::MenuBar>) {
				auto const item_font = inherited_font_size.value_or(13.0f);
				auto width = 0.0f;
				if (!state.title.empty()) {
					width += MeasureText(state.title, 14.0f).width + 16.0f;
				}
				for (auto const& entry : state.entries) {
					width += MeasureText(entry.title, item_font).width + 16.0f;
				}
				return fyuu_ui::Size{ width, 24.0f };
			}
			else if constexpr (std::same_as<State, fyuu_ui::SceneView>) {
				return fyuu_ui::Size{ 640.0f, 360.0f };
			}
			else if constexpr (std::same_as<State, fyuu_ui::Window>) {
				return state.size;
			}
			else {
				return fyuu_ui::Size{};
			}
			},
			widget
		);
	}

	fyuu_ui::Size MeasureStack(
		fyuu_ui::Orientation orientation,
		float spacing,
		std::span<fyuu_ui::Size const> children
	) {
		fyuu_ui::Size result;
		for (auto const& child : children) {
			if (orientation == fyuu_ui::Orientation::Horizontal) {
				result.width += child.width;
				result.height = std::max(result.height, child.height);
			}
			else {
				result.width = std::max(result.width, child.width);
				result.height += child.height;
			}
		}
		auto const gap_count = children.empty() ? 0.0f : static_cast<float>(children.size() - 1u);
		if (orientation == fyuu_ui::Orientation::Horizontal) {
			result.width += gap_count * spacing;
		}
		else {
			result.height += gap_count * spacing;
		}
		return result;
	}

	fyuu_ui::Size MeasureContainer(
		fyuu_ui::Container const& container,
		std::span<fyuu_ui::Size const> children,
		std::span<fyuu_ui::LayoutProperties const> properties
	) {
		return std::visit(
			[&children, &properties](auto const& state) {
				using State = std::remove_cvref_t<decltype(state)>;
			if constexpr (
				std::same_as<State, fyuu_ui::Overlay> ||
				std::same_as<State, fyuu_ui::WindowLayer>
			) {
				return MaximumSize(children);
			}
			else if constexpr (std::same_as<State, fyuu_ui::StackPanel>) {
				return MeasureStack(state.orientation, state.spacing, children);
			}
			else if constexpr (std::same_as<State, fyuu_ui::WrapPanel>) {
				auto const spacing = state.orientation == fyuu_ui::Orientation::Horizontal ?
					state.horizontal_spacing : state.vertical_spacing;
				return MeasureStack(state.orientation, spacing, children);
			}
			else if constexpr (std::same_as<State, fyuu_ui::Grid>) {
				auto columns = std::max<std::size_t>(state.columns, 1u);
				auto rows = std::max<std::size_t>(state.rows, 1u);
				for (auto const& property : properties) {
					columns = std::max<std::size_t>(
						columns,
						static_cast<std::size_t>(property.grid.column + std::max(property.grid.column_span, 1u))
					);
					rows = std::max<std::size_t>(
						rows,
						static_cast<std::size_t>(property.grid.row + std::max(property.grid.row_span, 1u))
					);
				}
				std::vector<float> column_widths(columns);
				std::vector<float> row_heights(rows);
				for (std::size_t index = 0u; index < children.size(); ++index) {
					auto const& grid = properties[index].grid;
					auto const column_span = std::max(grid.column_span, 1u);
					auto const row_span = std::max(grid.row_span, 1u);
					auto const column_width = children[index].width / static_cast<float>(column_span);
					auto const row_height = children[index].height / static_cast<float>(row_span);
					for (auto column = grid.column; column < grid.column + column_span; ++column) {
						column_widths[column] = std::max(column_widths[column], column_width);
					}
					for (auto row = grid.row; row < grid.row + row_span; ++row) {
						row_heights[row] = std::max(row_heights[row], row_height);
					}
				}
				auto column_width = 0.0f;
				for (auto const width : column_widths) {
					column_width += width;
				}
				auto row_height = 0.0f;
				for (auto const height : row_heights) {
					row_height += height;
				}
				return fyuu_ui::Size{
					column_width +
						static_cast<float>(columns - 1u) * state.column_spacing,
					row_height +
						static_cast<float>(rows - 1u) * state.row_spacing
				};
			}
			else if constexpr (
				std::same_as<State, fyuu_ui::Canvas> ||
				std::same_as<State, fyuu_ui::WindowLayer>
			) {
				fyuu_ui::Size result;
				for (std::size_t index = 0u; index < children.size(); ++index) {
					auto const& canvas = properties[index].canvas;
					result.width = std::max(
						result.width,
						canvas.left.value_or(0.0f) + children[index].width + canvas.right.value_or(0.0f)
					);
					result.height = std::max(
						result.height,
						canvas.top.value_or(0.0f) + children[index].height + canvas.bottom.value_or(0.0f)
					);
				}
				return result;
			}
			else if constexpr (std::same_as<State, fyuu_ui::UniformGrid>) {
				auto const maximum = MaximumSize(children);
				auto columns = static_cast<std::size_t>(state.columns);
				auto rows = static_cast<std::size_t>(state.rows);
				if (columns == 0u && rows == 0u) {
					columns = children.empty() ? 1u : static_cast<std::size_t>(std::ceil(std::sqrt(children.size())));
				}
				if (columns == 0u) {
					columns = std::max<std::size_t>((children.size() + rows - 1u) / rows, 1u);
				}
				rows = std::max<std::size_t>(
					rows,
					std::max<std::size_t>((children.size() + columns - 1u) / columns, 1u)
				);
				return fyuu_ui::Size{
					maximum.width * static_cast<float>(columns) +
						static_cast<float>(columns - 1u) * state.horizontal_spacing,
					maximum.height * static_cast<float>(rows) +
						static_cast<float>(rows - 1u) * state.vertical_spacing
				};
			}
			else if constexpr (std::same_as<State, fyuu_ui::SplitView>) {
				if (children.size() > 2u) {
					throw std::logic_error{ "SplitView accepts at most two child nodes." };
				}
				if (children.empty()) {
					return fyuu_ui::Size{};
				}
				if (children.size() == 1u) {
					return children.front();
				}
				auto const first = children.front();
				auto const second = children[1u];
				if (state.orientation == fyuu_ui::Orientation::Horizontal) {
					return fyuu_ui::Size{
						first.width + second.width + state.spacing,
						std::max(first.height, second.height)
					};
				}
				return fyuu_ui::Size{
					std::max(first.width, second.width),
					first.height + second.height + state.spacing
				};
			}
			else {
				auto horizontal_extent = 0.0f;
				auto vertical_extent = 0.0f;
				auto center_width = 0.0f;
				auto center_height = 0.0f;
				for (std::size_t index = 0u; index < children.size(); ++index) {
					if (state.last_child_fill && index + 1u == children.size()) {
						center_width = std::max(center_width, children[index].width);
						center_height = std::max(center_height, children[index].height);
						continue;
					}
					auto const dock = properties[index].dock.dock;
					if (dock == fyuu_ui::Dock::Left || dock == fyuu_ui::Dock::Right) {
						horizontal_extent += children[index].width;
						center_height = std::max(center_height, children[index].height);
					}
					else {
						center_width = std::max(center_width, children[index].width);
						vertical_extent += children[index].height;
					}
					if (index + 1u < children.size()) {
						if (dock == fyuu_ui::Dock::Left || dock == fyuu_ui::Dock::Right) {
							horizontal_extent += state.spacing;
						}
						else {
							vertical_extent += state.spacing;
						}
					}
				}
				return fyuu_ui::Size{
					horizontal_extent + center_width,
					vertical_extent + center_height
				};
			}
			},
			container
		);
	}

	std::vector<fyuu_ui::Rect> ArrangeStack(
		fyuu_ui::Orientation orientation,
		float spacing,
		fyuu_ui::Rect const& bounds,
		std::span<fyuu_ui::Size const> desired
	) {
		std::vector<fyuu_ui::Rect> result(desired.size());
		auto cursor = orientation == fyuu_ui::Orientation::Horizontal ?
			bounds.position.x : bounds.position.y;
		for (std::size_t index = 0u; index < desired.size(); ++index) {
			if (orientation == fyuu_ui::Orientation::Horizontal) {
				result[index] = fyuu_ui::Rect{
					{ cursor, bounds.position.y },
					{ desired[index].width, bounds.size.height }
				};
				cursor += desired[index].width + spacing;
			}
			else {
				result[index] = fyuu_ui::Rect{
					{ bounds.position.x, cursor },
					{ bounds.size.width, desired[index].height }
				};
				cursor += desired[index].height + spacing;
			}
		}
		return result;
	}

	std::vector<fyuu_ui::Rect> ArrangeContainer(
		fyuu_ui::Container const& container,
		fyuu_ui::Rect const& bounds,
		std::span<fyuu_ui::Size const> desired,
		std::span<fyuu_ui::LayoutProperties const> properties
	) {
		std::vector<fyuu_ui::Rect> result(desired.size(), bounds);
		std::visit(
			[&bounds, &desired, &properties, &result](auto const& state) {
				using State = std::remove_cvref_t<decltype(state)>;
			if constexpr (std::same_as<State, fyuu_ui::StackPanel>) {
				result = ArrangeStack(state.orientation, state.spacing, bounds, desired);
			}
			else if constexpr (std::same_as<State, fyuu_ui::Grid>) {
				auto columns = std::max<std::size_t>(state.columns, 1u);
				auto rows = std::max<std::size_t>(state.rows, 1u);
				for (auto const& property : properties) {
					columns = std::max<std::size_t>(
						columns,
						static_cast<std::size_t>(property.grid.column + std::max(property.grid.column_span, 1u))
					);
					rows = std::max<std::size_t>(
						rows,
						static_cast<std::size_t>(property.grid.row + std::max(property.grid.row_span, 1u))
					);
				}
				auto const cell_width = std::max(
					0.0f,
					(bounds.size.width - static_cast<float>(columns - 1u) * state.column_spacing) /
						static_cast<float>(columns)
				);
				auto const cell_height = std::max(
					0.0f,
					(bounds.size.height - static_cast<float>(rows - 1u) * state.row_spacing) /
						static_cast<float>(rows)
				);
				for (std::size_t index = 0u; index < desired.size(); ++index) {
					auto const& grid = properties[index].grid;
					auto const column_span = std::max(grid.column_span, 1u);
					auto const row_span = std::max(grid.row_span, 1u);
					result[index] = fyuu_ui::Rect{
						{
							bounds.position.x + static_cast<float>(grid.column) * (cell_width + state.column_spacing),
							bounds.position.y + static_cast<float>(grid.row) * (cell_height + state.row_spacing)
						},
						{
							cell_width * static_cast<float>(column_span) +
								state.column_spacing * static_cast<float>(column_span - 1u),
							cell_height * static_cast<float>(row_span) +
								state.row_spacing * static_cast<float>(row_span - 1u)
						}
					};
				}
			}
			else if constexpr (std::same_as<State, fyuu_ui::UniformGrid>) {
				auto columns = static_cast<std::size_t>(state.columns);
				auto rows = static_cast<std::size_t>(state.rows);
				if (columns == 0u && rows == 0u) {
					columns = desired.empty() ? 1u : static_cast<std::size_t>(std::ceil(std::sqrt(desired.size())));
				}
				if (columns == 0u) {
					columns = std::max<std::size_t>((desired.size() + rows - 1u) / rows, 1u);
				}
				rows = std::max<std::size_t>(
					rows,
					std::max<std::size_t>((desired.size() + columns - 1u) / columns, 1u)
				);
				auto const cell_width = std::max(
					0.0f,
					(bounds.size.width - static_cast<float>(columns - 1u) * state.horizontal_spacing) /
						static_cast<float>(columns)
				);
				auto const cell_height = std::max(
					0.0f,
					(bounds.size.height - static_cast<float>(rows - 1u) * state.vertical_spacing) /
						static_cast<float>(rows)
				);
				for (std::size_t index = 0u; index < desired.size(); ++index) {
					auto const column = index % columns;
					auto const row = index / columns;
					result[index] = fyuu_ui::Rect{
						{
							bounds.position.x + static_cast<float>(column) * (cell_width + state.horizontal_spacing),
							bounds.position.y + static_cast<float>(row) * (cell_height + state.vertical_spacing)
						},
						{ cell_width, cell_height }
					};
				}
			}
			else if constexpr (std::same_as<State, fyuu_ui::SplitView>) {
				if (result.empty()) {
					return;
				}
				if (result.size() == 1u) {
					result.front() = bounds;
					return;
				}
				auto const split = std::clamp(state.split, 0.0f, 1.0f);
				if (state.orientation == fyuu_ui::Orientation::Horizontal) {
					auto const available = std::max(0.0f, bounds.size.width - state.spacing);
					auto const maximum_first = std::max(
						0.0f,
						available - std::min(state.minimum_second, available)
					);
					auto const first_width = std::clamp(
						available * split,
						std::min(state.minimum_first, maximum_first),
						maximum_first
					);
					result[0u] = fyuu_ui::Rect{ bounds.position, { first_width, bounds.size.height } };
					if (result.size() > 1u) {
						result[1u] = fyuu_ui::Rect{
							{ bounds.position.x + first_width + state.spacing, bounds.position.y },
							{ std::max(0.0f, bounds.size.width - first_width - state.spacing), bounds.size.height }
						};
					}
				}
				else {
					auto const available = std::max(0.0f, bounds.size.height - state.spacing);
					auto const maximum_first = std::max(
						0.0f,
						available - std::min(state.minimum_second, available)
					);
					auto const first_height = std::clamp(
						available * split,
						std::min(state.minimum_first, maximum_first),
						maximum_first
					);
					result[0u] = fyuu_ui::Rect{ bounds.position, { bounds.size.width, first_height } };
					if (result.size() > 1u) {
						result[1u] = fyuu_ui::Rect{
							{ bounds.position.x, bounds.position.y + first_height + state.spacing },
							{ bounds.size.width, std::max(0.0f, bounds.size.height - first_height - state.spacing) }
						};
					}
				}
			}
			else if constexpr (
				std::same_as<State, fyuu_ui::Canvas> ||
				std::same_as<State, fyuu_ui::WindowLayer>
			) {
				for (std::size_t index = 0u; index < desired.size(); ++index) {
					auto const& canvas = properties[index].canvas;
					auto const width = canvas.left.has_value() && canvas.right.has_value() ?
						std::max(0.0f, bounds.size.width - *canvas.left - *canvas.right) : desired[index].width;
					auto const height = canvas.top.has_value() && canvas.bottom.has_value() ?
						std::max(0.0f, bounds.size.height - *canvas.top - *canvas.bottom) : desired[index].height;
					auto const x = canvas.left.has_value() ? bounds.position.x + *canvas.left :
						(canvas.right.has_value() ? bounds.position.x + bounds.size.width - *canvas.right - width : bounds.position.x);
					auto const y = canvas.top.has_value() ? bounds.position.y + *canvas.top :
						(canvas.bottom.has_value() ? bounds.position.y + bounds.size.height - *canvas.bottom - height : bounds.position.y);
					result[index] = fyuu_ui::Rect{ { x, y }, { width, height } };
				}
			}
			else if constexpr (std::same_as<State, fyuu_ui::DockPanel>) {
				auto remaining = bounds;
				for (std::size_t index = 0u; index < desired.size(); ++index) {
					if (state.last_child_fill && index + 1u == desired.size()) {
						result[index] = remaining;
						continue;
					}
					auto const dock = properties[index].dock.dock;
					if (dock == fyuu_ui::Dock::Left || dock == fyuu_ui::Dock::Right) {
						auto const width = std::min(desired[index].width, remaining.size.width);
						auto const x = dock == fyuu_ui::Dock::Left ? remaining.position.x :
							remaining.position.x + remaining.size.width - width;
						result[index] = fyuu_ui::Rect{ { x, remaining.position.y }, { width, remaining.size.height } };
						remaining.size.width = std::max(0.0f, remaining.size.width - width - state.spacing);
						if (dock == fyuu_ui::Dock::Left) {
							remaining.position.x += width + state.spacing;
						}
					}
					else {
						auto const height = std::min(desired[index].height, remaining.size.height);
						auto const y = dock == fyuu_ui::Dock::Top ? remaining.position.y :
							remaining.position.y + remaining.size.height - height;
						result[index] = fyuu_ui::Rect{ { remaining.position.x, y }, { remaining.size.width, height } };
						remaining.size.height = std::max(0.0f, remaining.size.height - height - state.spacing);
						if (dock == fyuu_ui::Dock::Top) {
							remaining.position.y += height + state.spacing;
						}
					}
				}
			}
			else if constexpr (std::same_as<State, fyuu_ui::WrapPanel>) {
				auto cursor = bounds.position;
				auto line_extent = 0.0f;
				for (std::size_t index = 0u; index < desired.size(); ++index) {
					if (state.orientation == fyuu_ui::Orientation::Horizontal) {
						auto const line_start = bounds.position.x;
						auto const line_end = bounds.position.x + bounds.size.width;
						if (cursor.x != line_start && cursor.x + desired[index].width > line_end) {
							cursor.x = line_start;
							cursor.y += line_extent + state.vertical_spacing;
							line_extent = 0.0f;
						}
						result[index] = fyuu_ui::Rect{ cursor, desired[index] };
						cursor.x += desired[index].width + state.horizontal_spacing;
						line_extent = std::max(line_extent, desired[index].height);
					}
					else {
						auto const line_start = bounds.position.y;
						auto const line_end = bounds.position.y + bounds.size.height;
						if (cursor.y != line_start && cursor.y + desired[index].height > line_end) {
							cursor.y = line_start;
							cursor.x += line_extent + state.horizontal_spacing;
							line_extent = 0.0f;
						}
						result[index] = fyuu_ui::Rect{ cursor, desired[index] };
						cursor.y += desired[index].height + state.vertical_spacing;
						line_extent = std::max(line_extent, desired[index].width);
					}
				}
			}
			},
			container
		);
		return result;
	}

}

namespace fyuu_ui {

	VisualTree LogicalTree::BuildVisualTree(
		Size const& available_size,
		Theme const& theme
	) const {
		// NodeState stores an N-ary hierarchy as a left-child/right-sibling chain.
		// Expanding every child chain into this array creates a stable breadth-first
		// order without recursion. Parents always precede their children, which lets
		// the following passes select their required direction by iterating this one
		// array forward or backward.
		std::vector<std::uint64_t> order{ 0u };
		for (std::size_t index = 0u; index < order.size(); ++index) {
			auto child_id = m_nodes[order[index]]->child;
			while (child_id.has_value()) {
				order.emplace_back(*child_id);
				child_id = m_nodes[*child_id]->sibling;
			}
		}

		// Style inheritance follows the same parent-before-child order as arrange.
		// Only foreground and font size are copied from the parent. The current
		// node's override replaces either inherited value before descendants are
		// visited, so no traversal needs to walk back up the hierarchy.
		std::vector<std::optional<Color>> foregrounds(m_nodes.size());
		std::vector<std::optional<float>> font_sizes(m_nodes.size());
		for (auto const node_id : order) {
			if (m_nodes[node_id]->parent.has_value()) {
				auto const parent_id = *m_nodes[node_id]->parent;
				foregrounds[node_id] = foregrounds[parent_id];
				font_sizes[node_id] = font_sizes[parent_id];
			}
			if (m_nodes[node_id]->style.foreground.has_value()) {
				foregrounds[node_id] = m_nodes[node_id]->style.foreground;
			}
			if (m_nodes[node_id]->style.font_size.has_value()) {
				font_sizes[node_id] = m_nodes[node_id]->style.font_size;
			}
		}

		// Measure runs in reverse breadth-first order. Consequently, every child has
		// a desired size before its parent is measured. A container combines all of
		// those sizes according to its layout policy; a widget combines its intrinsic
		// size with the largest child because widget children occupy its content area.
		std::vector<LayoutResult> layout(m_nodes.size());
		for (auto iterator = order.rbegin(); iterator != order.rend(); ++iterator) {
			auto const node_id = *iterator;
			std::vector<Size> children;
			std::vector<LayoutProperties> properties;
			auto child_id = m_nodes[node_id]->child;
			while (child_id.has_value()) {
				children.emplace_back(layout[*child_id].desired_size);
				properties.emplace_back(m_nodes[*child_id]->layout);
				child_id = m_nodes[*child_id]->sibling;
			}
			auto const desired_size = std::visit(
				[&children, &font_sizes, &properties, node_id](auto const& content) {
					using Content = std::remove_cvref_t<decltype(content)>;
					if constexpr (std::same_as<Content, Container>) {
						return MeasureContainer(content, children, properties);
					}
					else {
						auto result = MeasureWidget(content, font_sizes[node_id]);
						if (std::holds_alternative<Window>(content)) {
							return result;
						}
						auto const child = MaximumSize(children);
						result.width = std::max(result.width, child.width);
						result.height = std::max(result.height, child.height);
						return result;
					}
				},
				m_nodes[node_id]->content
			);
			layout[node_id].desired_size = ApplyMeasureProperties(
				desired_size,
				m_nodes[node_id]->layout
			);
		}

		// The caller owns the root constraint. Negative dimensions have no useful
		// layout meaning, so the root viewport is normalized before arrangement.
		layout[0u].bounds = Rect{
			{},
			{
				std::max(available_size.width, 0.0f),
				std::max(available_size.height, 0.0f)
			}
		};
		// Arrange runs in forward order because a parent must receive its final bounds
		// before it can assign bounds to its children. Iterating each sibling chain
		// also preserves the insertion order expected by stack and grid containers.
		for (auto const node_id : order) {
			std::vector<std::uint64_t> children;
			std::vector<Size> desired;
			std::vector<LayoutProperties> properties;
			auto child_id = m_nodes[node_id]->child;
			while (child_id.has_value()) {
				children.emplace_back(*child_id);
				desired.emplace_back(layout[*child_id].desired_size);
				properties.emplace_back(m_nodes[*child_id]->layout);
				child_id = m_nodes[*child_id]->sibling;
			}
			if (children.empty()) {
				continue;
			}
			std::visit(
				[&layout, &children, &desired, &properties, &theme, node_id, this](auto const& content) {
					using Content = std::remove_cvref_t<decltype(content)>;
					std::vector<Rect> bounds;
					if constexpr (std::same_as<Content, Container>) {
						if (std::holds_alternative<WindowLayer>(content)) {
							for (std::size_t index = 0u; index < children.size(); ++index) {
								auto const* widget = std::get_if<Widget>(&m_nodes[children[index]]->content);
								if (widget == nullptr || !std::holds_alternative<Window>(*widget)) {
									throw std::logic_error{ "WindowLayer accepts only Window child nodes." };
								}
								auto const& window = std::get<Window>(*widget);
								properties[index].width = window.size.width;
								properties[index].height = window.size.height;
								properties[index].canvas.left = window.position.x;
								properties[index].canvas.top = window.position.y;
							}
						}
						bounds = ArrangeContainer(
							content,
							layout[node_id].bounds,
							desired,
							properties
						);
					}
					else {
						auto content_bounds = layout[node_id].bounds;
						if (std::holds_alternative<Window>(content)) {
							content_bounds.position.y += theme.window_title_height;
							content_bounds.size.height = std::max(
								0.0f,
								content_bounds.size.height - theme.window_title_height
							);
						}
						bounds.assign(children.size(), content_bounds);
					}
					for (std::size_t index = 0u; index < children.size(); ++index) {
						layout[children[index]].bounds = ApplyArrangeProperties(
							bounds[index],
							desired[index],
							properties[index]
						);
					}
				},
				m_nodes[node_id]->content
			);
		}

		// The final forward pass converts arranged logical nodes into visual nodes.
		// anchors maps each logical ID to the visual node beneath which its children
		// must be emitted. The visual root is created by VisualTree itself and always
		// occupies internal ID zero.
		VisualTree result{ PassKey<LogicalTree>{} };
		std::vector<std::optional<std::uint64_t>> anchors(m_nodes.size());
		anchors[0u] = 0u;
		for (auto const node_id : order) {
			if (node_id != 0u) {
				// Logical bounds are absolute. Each visual group stores only its offset
				// relative to the parent group so moving a parent moves its full subtree.
				auto const parent_id = *m_nodes[node_id]->parent;
				auto const& bounds = layout[node_id].bounds;
				auto const& parent_bounds = layout[parent_id].bounds;
				Visual transform = TransformVisual{
					Transform2D{
						{
							bounds.position.x - parent_bounds.position.x,
							bounds.position.y - parent_bounds.position.y
						}
					}
				};
				auto node = result.Insert(
					PassKey<LogicalTree>{},
					node_id,
					transform
				);
				result.AddChild(
					PassKey<LogicalTree>{},
					*anchors[parent_id],
					node
				);
				anchors[node_id] = node;
			}
			auto const local_bounds = Rect{ {}, layout[node_id].bounds.size };
			auto const& style_override = m_nodes[node_id]->style;
			auto AppendVisual = [&result, &anchors, node_id](Visual const& visual) {
				auto node = result.Insert(
					PassKey<LogicalTree>{},
					node_id,
					visual
				);
				result.AddChild(
					PassKey<LogicalTree>{},
					*anchors[node_id],
					node
				);
				return node;
			};
			std::visit(
				[
					&anchors,
					&AppendVisual,
					&font_sizes,
					&foregrounds,
					&local_bounds,
					&style_override,
					&theme,
					node_id,
					this
				](auto const& content) {
					using Content = std::remove_cvref_t<decltype(content)>;
					if constexpr (std::same_as<Content, Container>) {
					std::visit(
						[&anchors, &AppendVisual, &local_bounds, &theme, node_id, this](auto const& container) {
							using ContainerType = std::remove_cvref_t<decltype(container)>;
							if constexpr (
								std::same_as<ContainerType, Overlay> ||
								std::same_as<ContainerType, Canvas>
							) {
								if (container.clip_to_bounds) {
									// Replacing the anchor makes subsequently emitted logical children
									// descendants of the clip instead of unrelated visual siblings.
					anchors[node_id] = AppendVisual(ClipVisual{ local_bounds });
								}
							}
							else if constexpr (std::same_as<ContainerType, SplitView>) {
								auto const first_child = m_nodes[node_id]->child;
								if (!container.resizable || container.spacing <= 0.0f ||
									!first_child.has_value() ||
									!m_nodes[*first_child]->sibling.has_value()) {
									return;
								}
								auto divider = local_bounds;
								auto const split = std::clamp(container.split, 0.0f, 1.0f);
								if (container.orientation == Orientation::Horizontal) {
									auto const available = std::max(0.0f, local_bounds.size.width - container.spacing);
									auto const maximum_first = std::max(
										0.0f,
										available - std::min(container.minimum_second, available)
									);
									auto const first = std::clamp(
										available * split,
										std::min(container.minimum_first, maximum_first),
										maximum_first
									);
									divider.position.x += first;
									divider.size.width = container.spacing;
								}
								else {
									auto const available = std::max(0.0f, local_bounds.size.height - container.spacing);
									auto const maximum_first = std::max(
										0.0f,
										available - std::min(container.minimum_second, available)
									);
									auto const first = std::clamp(
										available * split,
										std::min(container.minimum_first, maximum_first),
										maximum_first
									);
									divider.position.y += first;
									divider.size.height = container.spacing;
								}
				AppendVisual(RectangleVisual{
					divider,
					theme.divider
								});
							}
						}, 
						content
					);
					}
					else {
					std::visit(
						[
							&AppendVisual,
							&font_sizes,
							&foregrounds,
							&local_bounds,
							&style_override,
							&theme,
							node_id
						](auto const& widget) {
							using WidgetType = std::remove_cvref_t<decltype(widget)>;
							if constexpr (std::same_as<WidgetType, TextBlock>) {
								AppendVisual(
									TextVisual{
										local_bounds,
									widget.text,
									foregrounds[node_id].value_or(widget.color),
									font_sizes[node_id].value_or(widget.font_size),
									std::nullopt
									}
								);
							}
							else if constexpr (std::same_as<WidgetType, Border>) {
								AppendVisual(
									RectangleVisual{
										local_bounds,
										style_override.background.value_or(widget.background)
									
								});
							}
							else if constexpr (std::same_as<WidgetType, Separator>) {
								AppendVisual(
									RectangleVisual{
										local_bounds,
										style_override.background.value_or(widget.color)
									}
								);
							}
							else if constexpr (std::same_as<WidgetType, ProgressBar>) {
								auto const style = ResolveStyle(
									theme.progress_bar,
									style_override,
									foregrounds[node_id],
									font_sizes[node_id],
									InteractionState::Normal,
									true,
									false,
									false
								);
								AppendVisual(RectangleVisual{
									local_bounds,
									style.visual.background
								});
								auto progress = 0.0f;
								if (widget.maximum > widget.minimum) {
									progress = std::clamp(
										(widget.value - widget.minimum) / (widget.maximum - widget.minimum),
										0.0f,
										1.0f
									);
								}
								AppendVisual(RectangleVisual{
									Rect{ local_bounds.position, { local_bounds.size.width * progress, local_bounds.size.height } },
									style_override.background.value_or(theme.progress_bar.selected.background)
								});
							}
							else if constexpr (
								std::same_as<WidgetType, Button> ||
								std::same_as<WidgetType, ToggleButton>
							) {
								auto selected = false;
								if constexpr (std::same_as<WidgetType, Button>) {
									selected = widget.default_button;
								}
								else if constexpr (std::same_as<WidgetType, ToggleButton>) {
									selected = widget.checked;
								}
								auto const style = ResolveStyle(
									theme.button,
									style_override,
									foregrounds[node_id],
									font_sizes[node_id],
									widget.interaction,
									widget.enabled,
									selected,
									false
								);
								AppendVisual(RectangleVisual{
									local_bounds,
									style.visual.background
								});
								AppendVisual(TextVisual{
									local_bounds,
									widget.title,
									style.visual.foreground,
									style.font_size,
									std::nullopt
								});
							}
							else if constexpr (
								std::same_as<WidgetType, CheckBox> ||
								std::same_as<WidgetType, RadioButton>
							) {
								auto checked = false;
								if constexpr (std::same_as<WidgetType, CheckBox>) {
									checked = widget.checked;
								}
								else {
									checked = widget.checked;
								}
								auto const indicator = Rect{
									{
										local_bounds.position.x,
										local_bounds.position.y +
											(local_bounds.size.height - theme.indicator_size) * 0.5f
									},
									{ theme.indicator_size, theme.indicator_size }
								};
								auto const style = ResolveStyle(
									theme.indicator,
									style_override,
									foregrounds[node_id],
									font_sizes[node_id],
									widget.interaction,
									widget.enabled,
									checked,
									false
								);
								AppendVisual(RectangleVisual{
									indicator,
									style.visual.background
								});
								AppendVisual(TextVisual{
									Rect{
										{
											local_bounds.position.x + theme.indicator_size + theme.indicator_spacing,
											local_bounds.position.y
										},
										{
											std::max(
												0.0f,
												local_bounds.size.width - theme.indicator_size - theme.indicator_spacing
											),
											local_bounds.size.height
										}
									},
									widget.title,
									style.visual.foreground,
									style.font_size,
									std::nullopt
								});
							}
							else if constexpr (std::same_as<WidgetType, Slider>) {
								AppendVisual(RectangleVisual{
									local_bounds,
									Color{ 0.0f, 0.0f, 0.0f, 0.0f }
								});
								auto ratio = 0.0f;
								if (widget.maximum > widget.minimum) {
									ratio = std::clamp(
										(widget.value - widget.minimum) / (widget.maximum - widget.minimum),
										0.0f,
										1.0f
									);
								}
								auto track = local_bounds;
								auto thumb = local_bounds;
								if (widget.orientation == Orientation::Horizontal) {
									track.position.y += (track.size.height - theme.slider_track_thickness) * 0.5f;
									track.size.height = theme.slider_track_thickness;
									thumb.position.x += (thumb.size.width - theme.slider_thumb_size) * ratio;
									thumb.position.y += (thumb.size.height - theme.slider_thumb_size) * 0.5f;
									thumb.size = Size{ theme.slider_thumb_size, theme.slider_thumb_size };
								}
								else {
									track.position.x += (track.size.width - theme.slider_track_thickness) * 0.5f;
									track.size.width = theme.slider_track_thickness;
									thumb.position.x += (thumb.size.width - theme.slider_thumb_size) * 0.5f;
									thumb.position.y += (thumb.size.height - theme.slider_thumb_size) * (1.0f - ratio);
									thumb.size = Size{ theme.slider_thumb_size, theme.slider_thumb_size };
								}
								AppendVisual(RectangleVisual{
									track,
									theme.slider_track
								});
								AppendVisual(RectangleVisual{
									thumb,
									ResolveStyle(
										theme.slider_thumb,
										style_override,
										foregrounds[node_id],
										font_sizes[node_id],
										widget.interaction,
										true,
										true,
										false
									).visual.background
								});
							}
							else if constexpr (std::same_as<WidgetType, NumericBox>) {
								auto const style = ResolveStyle(
									theme.input,
									style_override,
									foregrounds[node_id],
									font_sizes[node_id],
									widget.interaction,
									!widget.read_only,
									false,
									widget.focused
								);
								AppendVisual(RectangleVisual{
									local_bounds,
									style.visual.background
								});
								AppendVisual(TextVisual{
									Rect{
										{ local_bounds.position.x + theme.horizontal_padding, local_bounds.position.y },
										{
											std::max(0.0f, local_bounds.size.width - theme.horizontal_padding * 2.0f),
											local_bounds.size.height
										}
									},
									std::format("{:.{}f}", widget.value, widget.decimal_places),
									style.visual.foreground,
									style.font_size,
									std::nullopt
								});
							}
							else if constexpr (std::same_as<WidgetType, MenuItem>) {
								auto const style = ResolveStyle(
									theme.menu_item,
									style_override,
									foregrounds[node_id],
									font_sizes[node_id],
									widget.interaction,
									widget.enabled,
									widget.checked,
									false
								);
								AppendVisual(RectangleVisual{
									local_bounds,
									style.visual.background
								});
								AppendVisual(TextVisual{
									Rect{
										{ local_bounds.position.x + 6.0f, local_bounds.position.y },
										{ std::max(0.0f, local_bounds.size.width - 12.0f), local_bounds.size.height }
									},
									widget.title,
									style.visual.foreground,
									style.font_size,
									std::nullopt
								});
							}
							else if constexpr (
								std::same_as<WidgetType, TextBox> ||
								std::same_as<WidgetType, SearchBox>
							) {
								auto const style = ResolveStyle(
									theme.input,
									style_override,
									foregrounds[node_id],
									font_sizes[node_id],
									InteractionState::Normal,
									true,
									false,
									widget.focused
								);
								AppendVisual(RectangleVisual{
									local_bounds,
									style.visual.background
								});
								AppendVisual(TextVisual{
									Rect{
										{ local_bounds.position.x + theme.horizontal_padding, local_bounds.position.y },
										{
											std::max(0.0f, local_bounds.size.width - theme.horizontal_padding * 2.0f),
											local_bounds.size.height
										}
									},
									widget.text.empty() && !widget.focused ? widget.placeholder : widget.text,
									widget.text.empty() && !widget.focused
										? theme.muted_text
										: style.visual.foreground,
									style.font_size,
									widget.focused
										? std::optional<std::size_t>{
											(std::min)(widget.caret_offset, widget.text.size())
										}
										: std::optional<std::size_t>{}
								});
							}
							else if constexpr (std::same_as<WidgetType, SceneView>) {
								AppendVisual(
										RectangleVisual{
										local_bounds,
										style_override.background.value_or(widget.clear_color)
									}
								);
							}
							else if constexpr (std::same_as<WidgetType, Window>) {
								// The non-client area owns the title and close affordance.
								// Logical children are arranged only inside the client area.
								auto client_bounds = local_bounds;
								client_bounds.position.y += theme.window_title_height;
								client_bounds.size.height = std::max(
									0.0f,
									client_bounds.size.height - theme.window_title_height
								);
								AppendVisual(RectangleVisual{
									client_bounds,
									style_override.background.value_or(theme.window_client)
								});
								auto title_bounds = local_bounds;
								title_bounds.size.height = std::min(
									theme.window_title_height,
									local_bounds.size.height
								);
								AppendVisual(GradientRectangleVisual{
									title_bounds,
									widget.active ?
										theme.window_non_client_glass : theme.window_non_client_inactive_glass,
									widget.active ?
										theme.window_non_client : theme.window_non_client_inactive,
									HitTestRole::WindowNonClient
								});
								AppendVisual(RectangleVisual{
									{
										title_bounds.position,
										{ title_bounds.size.width, 1.0f }
									},
									theme.window_non_client_highlight,
									HitTestRole::WindowNonClient
								});
								title_bounds.position.x += theme.window_horizontal_padding;
								title_bounds.size.width = std::max(
									0.0f,
									title_bounds.size.width - theme.window_horizontal_padding * 2.0f -
										theme.window_non_client_button_width
								);
								AppendVisual(TextVisual{
									title_bounds,
									widget.title,
									foregrounds[node_id].value_or(
										widget.active ? theme.window_title : theme.window_title_inactive
									),
									font_sizes[node_id].value_or(theme.window_font_size),
									std::nullopt
								});
								if (widget.closable) {
									auto close_bounds = title_bounds;
									close_bounds.position.x = std::max(
										0.0f,
										local_bounds.size.width - theme.window_non_client_button_width
									);
									close_bounds.size.width = std::min(
										theme.window_non_client_button_width,
										local_bounds.size.width
									);
									auto button_bounds = close_bounds;
									button_bounds.position.x += 3.0f;
									button_bounds.position.y += 5.0f;
									button_bounds.size.width = std::max(0.0f, button_bounds.size.width - 8.0f);
									button_bounds.size.height = std::max(0.0f, button_bounds.size.height - 10.0f);
									auto button_top = theme.window_non_client_button_highlight;
									auto button_bottom = theme.window_non_client_button;
									if (widget.non_client_button_interaction == InteractionState::Hovered) {
										button_top = theme.window_non_client_button_hovered_highlight;
										button_bottom = theme.window_non_client_button_hovered;
									}
									else if (widget.non_client_button_interaction == InteractionState::Pressed) {
										button_top = theme.window_non_client_button_pressed_highlight;
										button_bottom = theme.window_non_client_button_pressed;
									}
									AppendVisual(GradientRectangleVisual{
										button_bounds,
										button_top,
										button_bottom,
										HitTestRole::WindowNonClientButton
									});
									auto const close_center = Point{
										button_bounds.position.x + button_bounds.size.width * 0.5f,
										button_bounds.position.y + button_bounds.size.height * 0.5f
									};
									constexpr auto close_radius = 4.0f;
									AppendVisual(LineVisual{
										{ close_center.x - close_radius, close_center.y - close_radius },
										{ close_center.x + close_radius, close_center.y + close_radius },
										theme.window_non_client_button_foreground,
										1.25f
									});
									AppendVisual(LineVisual{
										{ close_center.x + close_radius, close_center.y - close_radius },
										{ close_center.x - close_radius, close_center.y + close_radius },
										theme.window_non_client_button_foreground,
										1.25f
									});
								}
								AppendVisual(RectangleVisual{
									{
										{ 0.0f, std::max(0.0f, theme.window_title_height - 1.0f) },
										{ local_bounds.size.width, 1.0f }
									},
									theme.window_non_client_shadow
								});
								AppendVisual(RectangleVisual{
									{ {}, { local_bounds.size.width, 1.0f } },
									theme.window_border
								});
								AppendVisual(RectangleVisual{
									{
										{ 0.0f, std::max(0.0f, local_bounds.size.height - 1.0f) },
										{ local_bounds.size.width, 1.0f }
									},
									theme.window_border
								});
								AppendVisual(RectangleVisual{
									{ {}, { 1.0f, local_bounds.size.height } },
									theme.window_border
								});
								AppendVisual(RectangleVisual{
									{
										{ std::max(0.0f, local_bounds.size.width - 1.0f), 0.0f },
										{ 1.0f, local_bounds.size.height }
									},
									theme.window_border
								});
								if (widget.resizable) {
									auto const resize_border = std::min(
										theme.window_resize_border,
										std::min(local_bounds.size.width, local_bounds.size.height) * 0.5f
									);
									auto const corner_size = resize_border * 2.0f;
									AppendVisual(HitTestVisual{
										{ {}, { resize_border, local_bounds.size.height } },
										HitTestRole::WindowResize,
										WindowResizeRegion::Left
									});
									AppendVisual(HitTestVisual{
										{ {}, { local_bounds.size.width, resize_border } },
										HitTestRole::WindowResize,
										WindowResizeRegion::Top
									});
									AppendVisual(HitTestVisual{
										{
											{ std::max(0.0f, local_bounds.size.width - resize_border), 0.0f },
											{ resize_border, local_bounds.size.height }
										},
										HitTestRole::WindowResize,
										WindowResizeRegion::Right
									});
									AppendVisual(HitTestVisual{
										{
											{ 0.0f, std::max(0.0f, local_bounds.size.height - resize_border) },
											{ local_bounds.size.width, resize_border }
										},
										HitTestRole::WindowResize,
										WindowResizeRegion::Bottom
									});
									AppendVisual(HitTestVisual{
										{ {}, { corner_size, corner_size } },
										HitTestRole::WindowResize,
										WindowResizeRegion::TopLeft
									});
									AppendVisual(HitTestVisual{
										{
											{ std::max(0.0f, local_bounds.size.width - corner_size), 0.0f },
											{ corner_size, corner_size }
										},
										HitTestRole::WindowResize,
										WindowResizeRegion::TopRight
									});
									AppendVisual(HitTestVisual{
										{
											{ 0.0f, std::max(0.0f, local_bounds.size.height - corner_size) },
											{ corner_size, corner_size }
										},
										HitTestRole::WindowResize,
										WindowResizeRegion::BottomLeft
									});
									AppendVisual(HitTestVisual{
										{
											{
												std::max(0.0f, local_bounds.size.width - corner_size),
												std::max(0.0f, local_bounds.size.height - corner_size)
											},
											{ corner_size, corner_size }
										},
										HitTestRole::WindowResize,
										WindowResizeRegion::BottomRight
									});
								}
							}
							else if constexpr (std::same_as<WidgetType, fyuu_ui::MenuBar>) {
								auto const item_font = font_sizes[node_id].value_or(theme.menu_item.font_size);
								auto const bar_height = local_bounds.size.height;
								// Bar background.
								AppendVisual(RectangleVisual{ local_bounds, theme.panel });
								// Optional brand label.
								auto cursor_x = 0.0f;
								if (!widget.title.empty()) {
									auto const text = MeasureText(widget.title, 14.0f);
									AppendVisual(TextVisual{
										Rect{ { 8.0f, 0.0f }, { text.width, bar_height } },
										widget.title,
										theme.text,
										14.0f,
										std::nullopt
									});
									cursor_x = text.width + 16.0f;
								}
								// Bar items; record each item's left offset for popup placement.
								std::vector<float> bar_rects;
								bar_rects.reserve(widget.entries.size());
								for (std::size_t i = 0u; i < widget.entries.size(); ++i) {
									auto const& entry = widget.entries[i];
									auto const width = MeasureText(entry.title, item_font).width + 16.0f;
									auto const item_rect = Rect{ { cursor_x, 0.0f }, { width, bar_height } };
									bar_rects.push_back(cursor_x);
									cursor_x += width;
									auto const is_open = widget.open_path.has_value() && (*widget.open_path)[0u] == i;
									auto const is_hover = widget.hover_path.has_value() && (*widget.hover_path)[0u] == i;
									auto const is_pressed = widget.pressed_path.has_value() && (*widget.pressed_path)[0u] == i;
									auto const interaction = is_pressed ? fyuu_ui::InteractionState::Pressed
										: (is_hover ? fyuu_ui::InteractionState::Hovered : fyuu_ui::InteractionState::Normal);
									auto const style = ResolveStyle(
										theme.menu_item, style_override, foregrounds[node_id], font_sizes[node_id],
										interaction, true, is_open, false
									);
									AppendVisual(RectangleVisual{ item_rect, style.visual.background });
									AppendVisual(TextVisual{
										Rect{ { item_rect.position.x + 6.0f, item_rect.position.y },
											{ std::max(0.0f, item_rect.size.width - 12.0f), item_rect.size.height } },
										entry.title, style.visual.foreground, style.font_size, std::nullopt
									});
									AppendVisual(HitTestVisual{
										item_rect, fyuu_ui::HitTestRole::MenuContent, fyuu_ui::WindowResizeRegion::None,
										fyuu_ui::MenuPath{ { static_cast<std::uint32_t>(i) } }
									});
								}
								// Open dropdown chain (nested cascading panels), only when open_path is engaged.
								if (widget.open_path.has_value()) {
									auto const& open = *widget.open_path;
									if (!open.empty() && open[0u] < widget.entries.size() && !widget.entries[open[0u]].children.empty()) {
										auto const* menu_entries = widget.entries[open[0u]].children.data();
										auto menu_count = widget.entries[open[0u]].children.size();
										auto panel_origin = fyuu_ui::Point{ bar_rects[open[0u]], bar_height + 8.0f };
										for (std::size_t level = 0u; level < open.size(); ++level) {
											auto panel_width = 180.0f;
											for (std::size_t i = 0u; i < menu_count; ++i) {
												panel_width = std::max(panel_width, MeasureText(menu_entries[i].title, item_font).width + 16.0f);
											}
											auto const panel = fyuu_ui::Rect{ panel_origin, { panel_width, static_cast<float>(menu_count) * 22.0f } };
											AppendVisual(RectangleVisual{ panel, theme.raised_surface });
											for (std::size_t i = 0u; i < menu_count; ++i) {
												auto const item_rect = fyuu_ui::Rect{
													{ panel_origin.x, panel_origin.y + static_cast<float>(i) * 22.0f },
													{ panel_width, 22.0f }
												};
												std::vector<std::uint32_t> path(open.begin(), open.begin() + level + 1);
												path.push_back(static_cast<std::uint32_t>(i));
												auto const is_hover = widget.hover_path.has_value() && *widget.hover_path == path;
												auto const is_pressed = widget.pressed_path.has_value() && *widget.pressed_path == path;
												auto const selected = (level + 1u < open.size() && open[level + 1u] == i) || menu_entries[i].checked;
												auto const interaction = is_pressed ? fyuu_ui::InteractionState::Pressed
													: (is_hover ? fyuu_ui::InteractionState::Hovered : fyuu_ui::InteractionState::Normal);
												auto const style = ResolveStyle(
													theme.menu_item, style_override, foregrounds[node_id], font_sizes[node_id],
													interaction, menu_entries[i].enabled, selected, false
												);
												AppendVisual(RectangleVisual{ item_rect, style.visual.background });
												AppendVisual(TextVisual{
													Rect{ { item_rect.position.x + 6.0f, item_rect.position.y },
														{ std::max(0.0f, item_rect.size.width - 12.0f), item_rect.size.height } },
													menu_entries[i].title, style.visual.foreground, style.font_size, std::nullopt
												});
												if (!menu_entries[i].children.empty()) {
													AppendVisual(TextVisual{
														Rect{ { item_rect.position.x + item_rect.size.width - 18.0f, item_rect.position.y },
															{ 14.0f, item_rect.size.height } },
														"\u203A", theme.muted_text, style.font_size, std::nullopt
													});
												}
												AppendVisual(HitTestVisual{
													item_rect, fyuu_ui::HitTestRole::MenuContent, fyuu_ui::WindowResizeRegion::None,
													fyuu_ui::MenuPath{ std::move(path) }
												});
											}
											if (level + 1u < open.size()) {
												auto const parent_index = open[level + 1u];
												if (parent_index < menu_count) {
													auto const& parent_item = menu_entries[parent_index];
													panel_origin = fyuu_ui::Point{
														panel.position.x + panel.size.width + 6.0f,
														panel.position.y + static_cast<float>(parent_index) * 22.0f
													};
													menu_entries = parent_item.children.data();
													menu_count = parent_item.children.size();
												}
											}
										}
									}
								}
							}

							else {
								static_assert(std::same_as<WidgetType, Spacer>);
							}
						}, 
						content
					);
					}
				},
				m_nodes[node_id]->content
			);
		}
		return result;
	}

}
