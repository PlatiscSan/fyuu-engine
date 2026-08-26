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
	// Combines typed lambdas into one overload set for the outer Content variant.
	// Concrete Widget and Container variants use the named Measure/Arrange overloads
	// below, keeping type policy out of the BuildVisualTree orchestration code.
	template <class... Visitors> struct VariantVisitor : Visitors... {
		using Visitors::operator()...;
	};
	template <class... Visitors> VariantVisitor(Visitors...) -> VariantVisitor<Visitors...>;

	// Returns the union size used by overlay-like containers and widget content.
	fyuu_ui::Size MaximumSize(std::span<fyuu_ui::Size const> sizes) {
		fyuu_ui::Size result;
		for (auto const& size : sizes) {
			result.width = std::max(result.width, size.width);
			result.height = std::max(result.height, size.height);
		}
		return result;
	}

	struct SplitGeometry {
		float first;
		float second;
	};

	// Resolves the normalized split once and applies both panes' minimum sizes.
	// Measure, arrange, and divider emission must use the same geometry rule.
	SplitGeometry ResolveSplit(fyuu_ui::SplitView const& split, float extent) {
		auto const available = std::max(0.0f, extent - split.spacing);
		auto const maximum_first =
		    std::max(0.0f, available - std::min(split.minimum_second, available));
		auto const first = std::clamp(
		    available * std::clamp(split.split, 0.0f, 1.0f),
		    std::min(split.minimum_first, maximum_first),
		    maximum_first
		);
		return {first, std::max(0.0f, available - first)};
	}

	fyuu_ui::Rect WindowClientBounds(fyuu_ui::Rect bounds, fyuu_ui::Theme const& theme) {
		bounds.position.y += theme.window_title_height;
		bounds.size.height = std::max(0.0f, bounds.size.height - theme.window_title_height);
		return bounds;
	}

	// Applies explicit/min/max constraints to the content dimension. Margins are
	// deliberately added by ApplyMeasureProperties after this function returns.
	float ClampDimension(
	    float desired,
	    std::optional<float> explicit_size,
	    float minimum,
	    std::optional<float> maximum
	) {
		auto result = explicit_size.value_or(desired);
		result = std::max(result, minimum);
		if (maximum) {
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
		    ) + properties.margin.left +
		        properties.margin.right,
		    ClampDimension(
		        desired.height,
		        properties.height,
		        properties.minimum_height,
		        properties.maximum_height
		    ) + properties.margin.top +
		        properties.margin.bottom
		};
	}

	// Converts a parent-assigned slot into the node's final content rectangle by
	// consuming margin and applying alignment. Stretch applies only without an
	// explicit size on that axis.
	fyuu_ui::Rect ApplyArrangeProperties(
	    fyuu_ui::Rect const& slot,
	    fyuu_ui::Size const& desired,
	    fyuu_ui::LayoutProperties const& properties
	) {
		auto const available_width =
		    std::max(0.0f, slot.size.width - properties.margin.left - properties.margin.right);
		auto const available_height =
		    std::max(0.0f, slot.size.height - properties.margin.top - properties.margin.bottom);
		auto const desired_width =
		    std::max(0.0f, desired.width - properties.margin.left - properties.margin.right);
		auto const desired_height =
		    std::max(0.0f, desired.height - properties.margin.top - properties.margin.bottom);
		auto width =
		    properties.horizontal_alignment == fyuu_ui::Alignment::Stretch && !properties.width ?
		    available_width :
		    std::min(desired_width, available_width);
		auto height =
		    properties.vertical_alignment == fyuu_ui::Alignment::Stretch && !properties.height ?
		    available_height :
		    std::min(desired_height, available_height);
		auto x = slot.position.x + properties.margin.left;
		auto y = slot.position.y + properties.margin.top;
		if (properties.horizontal_alignment == fyuu_ui::Alignment::Center) {
			x += (available_width - width) * 0.5f;
		} else if (properties.horizontal_alignment == fyuu_ui::Alignment::End) {
			x += available_width - width;
		}
		if (properties.vertical_alignment == fyuu_ui::Alignment::Center) {
			y += (available_height - height) * 0.5f;
		} else if (properties.vertical_alignment == fyuu_ui::Alignment::End) {
			y += available_height - height;
		}
		return fyuu_ui::Rect{{x, y}, {width, height}};
	}

	// ---- Widget intrinsic measurement -----------------------------------------
	// Every Widget alternative has a Measure overload with the same signature.
	// Adding a new control therefore produces a compile error here until its sizing
	// policy is supplied; no default branch can silently give it a zero size.
	fyuu_ui::Size Measure(
	    fyuu_ui::TextBlock const& widget,
	    std::optional<float> font_size,
	    fyuu_ui::TextMeasurer const& measure_text
	) {
		return measure_text(widget.text, font_size.value_or(widget.font_size));
	}

	fyuu_ui::Size Measure(
	    fyuu_ui::Border const&,
	    std::optional<float>,
	    fyuu_ui::TextMeasurer const&
	) {
		return {};
	}

	// These title-bearing controls intentionally share one sizing policy. The
	// constraint is the dispatch table: widening it opts another type into that rule.
	template <class WidgetType>
	    requires std::same_as<WidgetType, fyuu_ui::Button> ||
	    std::same_as<WidgetType, fyuu_ui::FileItem> ||
	    std::same_as<WidgetType, fyuu_ui::ToggleButton> ||
	    std::same_as<WidgetType, fyuu_ui::CheckBox>
	fyuu_ui::Size Measure(
	    WidgetType const& widget,
	    std::optional<float> font_size,
	    fyuu_ui::TextMeasurer const& measure_text
	) {
		auto const text = measure_text(widget.title, font_size.value_or(14.0f));
		return {text.width + 20.0f, std::max(text.height + 10.0f, 28.0f)};
	}

	fyuu_ui::Size Measure(
	    fyuu_ui::Slider const& widget,
	    std::optional<float>,
	    fyuu_ui::TextMeasurer const&
	) {
		return widget.orientation == fyuu_ui::Orientation::Horizontal ?
		    fyuu_ui::Size{120.0f, 20.0f} :
		    fyuu_ui::Size{20.0f, 120.0f};
	}

	// TextBox and NumericBox currently share the same editor chrome dimensions.
	template <class WidgetType>
	    requires std::same_as<WidgetType, fyuu_ui::TextBox> ||
	    std::same_as<WidgetType, fyuu_ui::NumericBox>
	fyuu_ui::Size Measure(WidgetType const&, std::optional<float>, fyuu_ui::TextMeasurer const&) {
		return {160.0f, 28.0f};
	}

	fyuu_ui::Size Measure(
	    fyuu_ui::MenuBar const& widget,
	    std::optional<float> font_size,
	    fyuu_ui::TextMeasurer const& measure_text
	) {
		auto const item_font = font_size.value_or(13.0f);
		auto width = widget.title.empty() ? 0.0f : measure_text(widget.title, 14.0f).width + 16.0f;
		for (auto const& entry : widget.entries) {
			width += measure_text(entry.title, item_font).width + 16.0f;
		}
		return {width, 24.0f};
	}

	fyuu_ui::Size Measure(
	    fyuu_ui::SceneView const&,
	    std::optional<float>,
	    fyuu_ui::TextMeasurer const&
	) {
		return {640.0f, 360.0f};
	}

	fyuu_ui::Size Measure(
	    fyuu_ui::Window const& widget,
	    std::optional<float>,
	    fyuu_ui::TextMeasurer const&
	) {
		return widget.size;
	}

	fyuu_ui::Size MeasureWidget(
	    fyuu_ui::Widget const& widget,
	    std::optional<float> font_size,
	    fyuu_ui::TextMeasurer const& measure_text
	) {
		// std::visit selects the concrete overload above; it contains no type switch.
		return std::visit(
		    [font_size, &measure_text](auto const& state) {
			    return Measure(state, font_size, measure_text);
		    },
		    widget
		);
	}

	// ---- Container desired-size aggregation -----------------------------------
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
			} else {
				result.width = std::max(result.width, child.width);
				result.height += child.height;
			}
		}
		auto const gap_count = children.empty() ? 0.0f : static_cast<float>(children.size() - 1u);
		if (orientation == fyuu_ui::Orientation::Horizontal) {
			result.width += gap_count * spacing;
		} else {
			result.height += gap_count * spacing;
		}
		return result;
	}

	// Overlay and WindowLayer both desire the union of their children even though
	// their arrange policies differ.
	template <class ContainerType>
	    requires std::same_as<ContainerType, fyuu_ui::Overlay> ||
	    std::same_as<ContainerType, fyuu_ui::WindowLayer>
	fyuu_ui::Size Measure(ContainerType const&, std::span<fyuu_ui::Size const> children) {
		return MaximumSize(children);
	}

	fyuu_ui::Size Measure(
	    fyuu_ui::StackPanel const& container,
	    std::span<fyuu_ui::Size const> children
	) {
		return MeasureStack(container.orientation, container.spacing, children);
	}

	fyuu_ui::Size Measure(fyuu_ui::ScrollView const&, std::span<fyuu_ui::Size const> children) {
		if (children.size() > 1u) {
			throw std::logic_error{"ScrollView accepts at most one child node."};
		}
		return children.empty() ? fyuu_ui::Size{} : children.front();
	}

	fyuu_ui::Size Measure(
	    fyuu_ui::SplitView const& container,
	    std::span<fyuu_ui::Size const> children
	) {
		if (children.size() > 2u) {
			throw std::logic_error{"SplitView accepts at most two child nodes."};
		}
		if (children.empty())
			return {};
		if (children.size() == 1u)
			return children.front();
		auto const first = children.front();
		auto const second = children[1u];
		return container.orientation == fyuu_ui::Orientation::Horizontal ?
		    fyuu_ui::Size{
		        first.width + second.width + container.spacing,
		        std::max(first.height, second.height)
		    } :
		    fyuu_ui::Size{
		        std::max(first.width, second.width),
		        first.height + second.height + container.spacing
		    };
	}

	fyuu_ui::Size MeasureContainer(
	    fyuu_ui::Container const& container,
	    std::span<fyuu_ui::Size const> children
	) {
		// Missing a concrete container overload is a compile-time error at this visit.
		return std::visit(
		    [children](auto const& state) {
			    return Measure(state, children);
		    },
		    container
		);
	}

	// ---- Container slot allocation --------------------------------------------
	// Arrange overloads return one slot per child in the original sibling order.
	std::vector<fyuu_ui::Rect> ArrangeStack(
	    fyuu_ui::Orientation orientation,
	    float spacing,
	    fyuu_ui::Rect const& bounds,
	    std::span<fyuu_ui::Size const> desired
	) {
		std::vector<fyuu_ui::Rect> result(desired.size());
		auto cursor =
		    orientation == fyuu_ui::Orientation::Horizontal ? bounds.position.x : bounds.position.y;
		for (std::size_t index = 0u; index < desired.size(); ++index) {
			if (orientation == fyuu_ui::Orientation::Horizontal) {
				result[index] = fyuu_ui::Rect{
				    {cursor, bounds.position.y},
				    {desired[index].width, bounds.size.height}
				};
				cursor += desired[index].width + spacing;
			} else {
				result[index] = fyuu_ui::Rect{
				    {bounds.position.x, cursor},
				    {bounds.size.width, desired[index].height}
				};
				cursor += desired[index].height + spacing;
			}
		}
		return result;
	}

	std::vector<fyuu_ui::Rect> Arrange(
	    fyuu_ui::Overlay const&,
	    fyuu_ui::Rect const& bounds,
	    std::span<fyuu_ui::Size const> desired,
	    std::span<fyuu_ui::LayoutProperties const>
	) {
		return {desired.size(), bounds};
	}

	std::vector<fyuu_ui::Rect> Arrange(
	    fyuu_ui::StackPanel const& container,
	    fyuu_ui::Rect const& bounds,
	    std::span<fyuu_ui::Size const> desired,
	    std::span<fyuu_ui::LayoutProperties const>
	) {
		return ArrangeStack(container.orientation, container.spacing, bounds, desired);
	}

	std::vector<fyuu_ui::Rect> Arrange(
	    fyuu_ui::ScrollView& container,
	    fyuu_ui::Rect const& bounds,
	    std::span<fyuu_ui::Size const> desired,
	    std::span<fyuu_ui::LayoutProperties const>
	) {
		if (desired.empty())
			return {};
		if (desired.size() > 1u) {
			throw std::logic_error{"ScrollView accepts at most one child node."};
		}
		auto const content_height = std::max(bounds.size.height, desired.front().height);
		container.viewport_extent = bounds.size.height;
		container.content_extent = content_height;
		auto const maximum_offset = std::max(0.0f, content_height - bounds.size.height);
		auto const offset = std::clamp(container.offset, 0.0f, maximum_offset);
		// Overflow reserves a narrow gutter so row backgrounds cannot paint over the
		// scrollbar emitted by ScrollView itself.
		auto const content_width = content_height > bounds.size.height ?
		    std::max(0.0f, bounds.size.width - 12.0f) :
		    bounds.size.width;
		return {fyuu_ui::Rect{
		    {bounds.position.x, bounds.position.y - offset},
		    {content_width, content_height}
		}};
	}

	std::vector<fyuu_ui::Rect> Arrange(
	    fyuu_ui::SplitView const& container,
	    fyuu_ui::Rect const& bounds,
	    std::span<fyuu_ui::Size const> desired,
	    std::span<fyuu_ui::LayoutProperties const>
	) {
		std::vector<fyuu_ui::Rect> result(desired.size(), bounds);
		if (result.size() < 2u)
			return result;
		auto const extent = container.orientation == fyuu_ui::Orientation::Horizontal ?
		    bounds.size.width :
		    bounds.size.height;
		auto const split = ResolveSplit(container, extent);
		if (container.orientation == fyuu_ui::Orientation::Horizontal) {
			result[0u] = {bounds.position, {split.first, bounds.size.height}};
			result[1u] = {
			    {bounds.position.x + split.first + container.spacing, bounds.position.y},
			    {split.second, bounds.size.height}
			};
		} else {
			result[0u] = {bounds.position, {bounds.size.width, split.first}};
			result[1u] = {
			    {bounds.position.x, bounds.position.y + split.first + container.spacing},
			    {bounds.size.width, split.second}
			};
		}
		return result;
	}

	std::vector<fyuu_ui::Rect> Arrange(
	    fyuu_ui::WindowLayer const&,
	    fyuu_ui::Rect const& bounds,
	    std::span<fyuu_ui::Size const> desired,
	    std::span<fyuu_ui::LayoutProperties const> properties
	) {
		// WindowLayer attached properties use CSS-like opposing anchors: specifying
		// both edges stretches the window; one edge preserves its desired extent.
		std::vector<fyuu_ui::Rect> result(desired.size(), bounds);
		for (std::size_t index = 0u; index < desired.size(); ++index) {
			auto const& window = properties[index].window;
			auto const width = window.left && window.right ?
			    std::max(0.0f, bounds.size.width - *window.left - *window.right) :
			    desired[index].width;
			auto const height = window.top && window.bottom ?
			    std::max(0.0f, bounds.size.height - *window.top - *window.bottom) :
			    desired[index].height;
			auto const x = window.left ? bounds.position.x + *window.left :
			                             bounds.position.x + bounds.size.width -
			        window.right.value_or(bounds.size.width - width) - width;
			auto const y = window.top ? bounds.position.y + *window.top :
			                            bounds.position.y + bounds.size.height -
			        window.bottom.value_or(bounds.size.height - height) - height;
			result[index] = {{x, y}, {width, height}};
		}
		return result;
	}

	std::vector<fyuu_ui::Rect> ArrangeContainer(
	    fyuu_ui::Container& container,
	    fyuu_ui::Rect const& bounds,
	    std::span<fyuu_ui::Size const> desired,
	    std::span<fyuu_ui::LayoutProperties const> properties
	) {
		// std::visit is only the variant adapter. All behavior lives in typed overloads.
		return std::visit(
		    [&bounds, desired, properties](auto& state) {
			    return Arrange(state, bounds, desired, properties);
		    },
		    container
		);
	}

} // namespace

namespace fyuu_ui {

	VisualTree LogicalTree::BuildVisualTree(
	    Size const& available_size,
	    Theme const& theme,
	    TextMeasurer const& measure_text
	) {
		// NodeState stores an N-ary hierarchy as a left-child/right-sibling chain.
		// Expanding every child chain into this array creates a stable breadth-first
		// order without recursion. Parents always precede their children, which lets
		// the following passes select their required direction by iterating this one
		// array forward or backward.
		std::vector<std::uint64_t> order{0u};
		for (std::size_t index = 0u; index < order.size(); ++index) {
			auto child_id = m_nodes[order[index]]->child;
			while (child_id != 0u) {
				order.emplace_back(child_id);
				child_id = m_nodes[child_id]->sibling;
			}
		}

		// Style inheritance follows the same parent-before-child order as arrange.
		// Only foreground and font size are copied from the parent. The current
		// node's override replaces either inherited value before descendants are
		// visited, so no traversal needs to walk back up the hierarchy.
		std::vector<std::optional<Color>> foregrounds(m_nodes.size());
		std::vector<std::optional<float>> font_sizes(m_nodes.size());
		for (auto const node_id : order) {
			if (node_id != 0u) {
				auto const parent_id = m_nodes[node_id]->parent;
				foregrounds[node_id] = foregrounds[parent_id];
				font_sizes[node_id] = font_sizes[parent_id];
			}
			if (m_nodes[node_id]->style.foreground) {
				foregrounds[node_id] = m_nodes[node_id]->style.foreground;
			}
			if (m_nodes[node_id]->style.font_size) {
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
			while (child_id != 0u) {
				children.emplace_back(layout[child_id].desired_size);
				properties.emplace_back(m_nodes[child_id]->layout);
				child_id = m_nodes[child_id]->sibling;
			}
			auto const desired_size = std::visit(
			    VariantVisitor{
			        [&children](Container const& content) {
				        return MeasureContainer(content, children);
			        },
			        [&children, &font_sizes, &measure_text, node_id](Widget const& content) {
				        auto result = MeasureWidget(content, font_sizes[node_id], measure_text);
				        if (std::holds_alternative<Window>(content))
					        return result;
				        auto const child = MaximumSize(children);
				        result.width = std::max(result.width, child.width);
				        result.height = std::max(result.height, child.height);
				        return result;
			        }
			    },
			    m_nodes[node_id]->content
			);
			layout[node_id].desired_size =
			    ApplyMeasureProperties(desired_size, m_nodes[node_id]->layout);
		}

		// The caller owns the root constraint. Negative dimensions have no useful
		// layout meaning, so the root viewport is normalized before arrangement.
		layout[0u].bounds =
		    Rect{{}, {std::max(available_size.width, 0.0f), std::max(available_size.height, 0.0f)}};
		// Arrange runs in forward order because a parent must receive its final bounds
		// before it can assign bounds to its children. Iterating each sibling chain
		// also preserves the insertion order expected by stack and grid containers.
		for (auto const node_id : order) {
			std::vector<std::uint64_t> children;
			std::vector<Size> desired;
			std::vector<LayoutProperties> properties;
			auto child_id = m_nodes[node_id]->child;
			while (child_id != 0u) {
				children.emplace_back(child_id);
				desired.emplace_back(layout[child_id].desired_size);
				properties.emplace_back(m_nodes[child_id]->layout);
				child_id = m_nodes[child_id]->sibling;
			}
			if (children.empty()) {
				continue;
			}
			std::visit(
			    VariantVisitor{
			        [&layout, &children, &desired, &properties, node_id, this](
			            Container& content
			        ) {
				        if (std::holds_alternative<WindowLayer>(content)) {
					        for (std::size_t index = 0u; index < children.size(); ++index) {
						        auto const* widget =
						            std::get_if<Widget>(&m_nodes[children[index]]->content);
						        if (widget == nullptr || !std::holds_alternative<Window>(*widget)) {
							        throw std::logic_error{
							            "WindowLayer accepts only Window child nodes."
							        };
						        }
						        auto const& window = std::get<Window>(*widget);
						        properties[index].width = window.size.width;
						        properties[index].height = window.size.height;
						        properties[index].window.left = window.position.x;
						        properties[index].window.top = window.position.y;
					        }
				        }
				        auto const bounds =
				            ArrangeContainer(content, layout[node_id].bounds, desired, properties);
				        for (std::size_t index = 0u; index < children.size(); ++index) {
					        layout[children[index]].bounds = ApplyArrangeProperties(
					            bounds[index],
					            desired[index],
					            properties[index]
					        );
				        }
			        },
			        [&layout, &children, &desired, &properties, &theme, node_id](
			            Widget const& content
			        ) {
				        auto content_bounds = layout[node_id].bounds;
				        if (std::holds_alternative<Window>(content)) {
					        content_bounds = WindowClientBounds(content_bounds, theme);
				        }
				        for (std::size_t index = 0u; index < children.size(); ++index) {
					        layout[children[index]].bounds = ApplyArrangeProperties(
					            content_bounds,
					            desired[index],
					            properties[index]
					        );
				        }
			        }
			    },
			    m_nodes[node_id]->content
			);
		}

		return EmitVisualTree(order, layout, foregrounds, font_sizes, theme, measure_text);
	}

} // namespace fyuu_ui
