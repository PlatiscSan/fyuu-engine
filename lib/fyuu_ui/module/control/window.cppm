module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>
#endif
export module fyuu_ui:control_window;
#if defined(__cpp_lib_modules)
import std;
#endif
import :control_common;
import :geometry;
export namespace fyuu_ui {
	/// A FyuuUI top-level window. Position and size are client-controlled logical
	/// values; WindowLayer owns stacking and active-window selection.
	struct Window {
		std::string title;
		Point position;
		Size size;
		bool closable = true;
		InteractionState non_client_button_interaction = InteractionState::Normal;
		Size minimum_size{160.0f, 96.0f};
		bool resizable = true;
		bool active = false;
	};
} // namespace fyuu_ui
