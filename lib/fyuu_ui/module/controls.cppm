module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <variant>
#endif
export module fyuu_ui:controls;
#if defined(__cpp_lib_modules)
import std;
#endif
export import :control_common;
export import :control_border;
export import :control_text_block;
export import :control_button;
export import :control_file_item;
export import :control_toggle_button;
export import :control_check_box;
export import :control_slider;
export import :control_text_box;
export import :control_numeric_box;
export import :control_menu_bar;
export import :control_scene_view;
export import :control_scroll_bar;
export import :control_window;
export namespace fyuu_ui {
	/// Closed set of leaf values a LogicalNode may own. Adding a control requires
	/// corresponding layout and emitter overloads so visitation stays exhaustive.
	using Widget = std::variant<
	    Border,
	    TextBlock,
	    Button,
	    FileItem,
	    ToggleButton,
	    CheckBox,
	    Slider,
	    TextBox,
	    NumericBox,
	    MenuBar,
	    SceneView,
	    Window>;
}
