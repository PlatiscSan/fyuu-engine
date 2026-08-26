module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>
#endif
export module fyuu_ui:control_toggle_button;
#if defined(__cpp_lib_modules)
import std;
#endif
import :control_common;
export namespace fyuu_ui {
	/// A command surface whose persistent checked state is independent of its
	/// transient pointer interaction state.
	struct ToggleButton {
		std::string title;
		bool checked = false;
		bool enabled = true;
		InteractionState interaction = InteractionState::Normal;
	};
} // namespace fyuu_ui
