module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>
#endif
export module fyuu_ui:control_button;
#if defined(__cpp_lib_modules)
import std;
#endif
import :control_common;
export namespace fyuu_ui {
	/// A focusable command surface. `default_button` is presentation metadata;
	/// activation remains an explicit routed ClickEvent from the host.
	struct Button {
		std::string title;
		bool enabled = true;
		bool default_button = false;
		InteractionState interaction = InteractionState::Normal;
	};
} // namespace fyuu_ui
