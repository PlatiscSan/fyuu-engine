module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <optional>
#endif // !defined(__cpp_lib_modules)

export module fyuu_ui:theme;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

import :geometry;

export namespace fyuu_ui {

	/// Describes the visual result of one widget state. It contains no behavior;
	/// layout resolves the appropriate state before emitting visual commands.
	struct VisualStyle {
		Color background;
		Color foreground;
	};

	/// Groups all state-dependent visuals and shared metrics for one widget family.
	struct WidgetStyle {
		VisualStyle normal;
		VisualStyle hovered;
		VisualStyle pressed;
		VisualStyle disabled;
		VisualStyle focused;
		VisualStyle selected;
		VisualStyle selected_hovered;
		float font_size = 14.0f;
	};

	/// Overrides one logical node without copying a complete WidgetStyle.
	/// Foreground and font size inherit through descendants; background applies
	/// only to the node on which it is set.
	struct StyleOverride {
		std::optional<Color> background;
		std::optional<Color> foreground;
		std::optional<float> font_size;
	};

	/// Owns the complete semantic palette and widget styles used to turn a
	/// LogicalTree into a VisualTree. Theme is a value object and has no global
	/// registration or lifetime requirements.
	struct Theme {
		Color background;
		Color surface;
		Color raised_surface;
		Color panel;
		Color text;
		Color muted_text;
		Color divider;
		Color slider_track;
		WidgetStyle button;
		WidgetStyle indicator;
		WidgetStyle input;
		WidgetStyle menu_item;
		WidgetStyle slider_thumb;
		WidgetStyle progress_bar;
		Color window_client;
		Color window_client_text;
		Color window_client_muted_text;
		Color window_non_client;
		Color window_non_client_glass;
		Color window_non_client_inactive;
		Color window_non_client_inactive_glass;
		Color window_non_client_highlight;
		Color window_non_client_shadow;
		Color window_border;
		Color window_title;
		Color window_title_inactive;
		Color window_non_client_button;
		Color window_non_client_button_highlight;
		Color window_non_client_button_foreground;
		Color window_non_client_button_hovered;
		Color window_non_client_button_hovered_highlight;
		Color window_non_client_button_pressed;
		Color window_non_client_button_pressed_highlight;
		float indicator_size = 16.0f;
		float indicator_spacing = 6.0f;
		float horizontal_padding = 8.0f;
		float slider_track_thickness = 4.0f;
		float slider_thumb_size = 14.0f;
		float window_title_height = 32.0f;
		float window_horizontal_padding = 12.0f;
		float window_font_size = 13.0f;
		float window_non_client_button_font_size = 14.0f;
		float window_non_client_button_width = 36.0f;
		float window_resize_border = 5.0f;
	};

	/// Creates FyuuUI's built-in dark theme. Callers may copy and modify the
	/// returned value before passing it to LogicalTree::BuildVisualTree.
	[[nodiscard]] inline Theme DarkTheme() noexcept {
		constexpr Color transparent{ 0.0f, 0.0f, 0.0f, 0.0f };
		constexpr Color background{ 0.045f, 0.052f, 0.068f, 1.0f };
		constexpr Color surface{ 0.105f, 0.118f, 0.150f, 1.0f };
		constexpr Color surface_hovered{ 0.145f, 0.165f, 0.210f, 1.0f };
		constexpr Color border{ 0.235f, 0.260f, 0.320f, 1.0f };
		constexpr Color accent{ 0.300f, 0.430f, 0.780f, 1.0f };
		constexpr Color accent_hovered{ 0.360f, 0.500f, 0.880f, 1.0f };
		constexpr Color accent_pressed{ 0.245f, 0.355f, 0.680f, 1.0f };
		constexpr Color text{ 0.855f, 0.875f, 0.915f, 1.0f };
		constexpr Color muted_text{ 0.480f, 0.510f, 0.575f, 1.0f };
		constexpr VisualStyle control{ surface, text };
		constexpr VisualStyle control_hovered{ surface_hovered, text };
		constexpr VisualStyle control_pressed{ accent_pressed, text };
		constexpr VisualStyle disabled{ surface, muted_text };
		constexpr VisualStyle focused{ surface, text };
		constexpr VisualStyle selected{ accent, text };
		constexpr VisualStyle selected_hovered{ accent_hovered, text };
		constexpr WidgetStyle control_style{
			control,
			control_hovered,
			control_pressed,
			disabled,
			focused,
			selected,
			selected_hovered,
			14.0f
		};
		constexpr WidgetStyle menu_style{
			{ transparent, text },
			{ surface_hovered, text },
			{ accent_pressed, text },
			{ transparent, muted_text },
			{ transparent, text },
			{ surface, text },
			{ surface_hovered, text },
			13.0f
		};
		return {
			background,
			{ 0.062f, 0.072f, 0.094f, 1.0f },
			{ 0.075f, 0.086f, 0.112f, 1.0f },
			{ 0.070f, 0.080f, 0.105f, 1.0f },
			text,
			muted_text,
			{ 0.165f, 0.185f, 0.230f, 1.0f },
			border,
			control_style,
			control_style,
			control_style,
			menu_style,
			control_style,
			control_style,
			{ 0.050f, 0.058f, 0.072f, 1.0f },
			{ 0.875f, 0.890f, 0.920f, 1.0f },
			{ 0.470f, 0.500f, 0.560f, 1.0f },
			{ 0.095f, 0.110f, 0.135f, 1.0f },
			{ 0.180f, 0.205f, 0.245f, 1.0f },
			{ 0.075f, 0.082f, 0.098f, 1.0f },
			{ 0.120f, 0.132f, 0.155f, 1.0f },
			{ 0.330f, 0.365f, 0.420f, 1.0f },
			{ 0.030f, 0.035f, 0.045f, 1.0f },
			{ 0.245f, 0.275f, 0.330f, 1.0f },
			{ 0.900f, 0.915f, 0.945f, 1.0f },
			{ 0.570f, 0.600f, 0.660f, 1.0f },
			{ 0.720f, 0.230f, 0.180f, 1.0f },
			{ 0.940f, 0.520f, 0.430f, 1.0f },
			{ 1.000f, 0.980f, 0.970f, 1.0f },
			{ 0.880f, 0.300f, 0.220f, 1.0f },
			{ 1.000f, 0.610f, 0.480f, 1.0f },
			{ 0.580f, 0.120f, 0.100f, 1.0f },
			{ 0.780f, 0.260f, 0.190f, 1.0f },
			16.0f,
			6.0f,
			8.0f,
			4.0f,
			14.0f,
			32.0f,
			12.0f,
			13.0f,
			14.0f,
			36.0f,
			5.0f
		};
	}

}
