module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>
#endif
export module fyuu_ui:control_check_box;
#if defined(__cpp_lib_modules)
import std;
#endif
import :control_common;
export namespace fyuu_ui {
	/// A focusable independent Boolean option with an adjacent text label.
	struct CheckBox {
		std::string title;
		bool checked = false;
		bool enabled = true;
		InteractionState interaction = InteractionState::Normal;
	};
} // namespace fyuu_ui
