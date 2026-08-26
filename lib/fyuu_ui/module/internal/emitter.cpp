module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <utility>
#include <vector>
#include <algorithm>
#include <optional>
#endif

module fyuu_ui:emitter;
#if defined(__cpp_lib_modules)
import std;
#endif
import :logical_tree;
import :theme;

namespace fyuu_ui::detail {
	/// Value-owned visual fragment returned by one emitter. `parent` indexes another
	/// entry in this fragment; `child_anchor` selects where logical children attach.
	struct EmitterOutput {
		struct Entry {
			Visual visual;
			std::optional<std::size_t> parent;
		};

		std::vector<Entry> entries;
		std::optional<std::size_t> child_anchor;

		std::size_t Append(Visual const& visual) {
			entries.emplace_back(visual, child_anchor);
			return entries.size() - 1u;
		}
		void SetAnchor(std::size_t index) noexcept {
			child_anchor = index;
		}
	};

	struct ResolvedWidgetStyle {
		VisualStyle visual;
		float font_size;
	};
	using OptionalColor = std::optional<Color>;
	using OptionalFloat = std::optional<float>;

	ResolvedWidgetStyle ResolveStyle(
	    WidgetStyle const& style,
	    StyleOverride const& local,
	    std::optional<Color> inherited_foreground,
	    std::optional<float> inherited_font_size,
	    InteractionState interaction,
	    bool enabled,
	    bool selected,
	    bool focused
	) noexcept {
		auto const* resolved = &style.normal;
		if (!enabled) {
			resolved = &style.disabled;
		} else {
			switch (interaction) {
				case InteractionState::Pressed: resolved = &style.pressed; break;
				case InteractionState::Hovered:
					resolved = selected ? &style.selected_hovered : &style.hovered;
					break;
				case InteractionState::Normal:
					resolved = selected ? &style.selected : focused ? &style.focused : &style.normal;
					break;
			}
		}
		auto visual = *resolved;
		visual.background = local.background.value_or(visual.background);
		visual.foreground = inherited_foreground.value_or(visual.foreground);
		return {visual, inherited_font_size.value_or(style.font_size)};
	}

	Rect PaddedTextBounds(Rect bounds, float padding) {
		bounds.position.x += padding;
		bounds.size.width = std::max(0.0f, bounds.size.width - padding * 2.0f);
		return bounds;
	}

	Rect WindowClientBounds(Rect bounds, Theme const& theme) {
		bounds.position.y += theme.window_title_height;
		bounds.size.height = std::max(0.0f, bounds.size.height - theme.window_title_height);
		return bounds;
	}

	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, Overlay const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, WindowLayer const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, StackPanel const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, SplitView const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, ScrollView const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, Border const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, TextBlock const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, Button const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, FileItem const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, ToggleButton const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, CheckBox const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, Slider const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, TextBox const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, NumericBox const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, MenuBar const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, SceneView const&);
	EmitterOutput Emit(Rect, StyleOverride const&, OptionalColor, OptionalFloat,
	                   Theme const&, TextMeasurer const&, bool, Size, Window const&);
} // namespace fyuu_ui::detail
