module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>
#endif
export module fyuu_ui:control_file_item;
#if defined(__cpp_lib_modules)
import std;
#endif
import :control_common;
export namespace fyuu_ui {
	/// A flat selectable row used by file browsers; action buttons remain Button.
	struct FileItem {
		std::string title;
		bool directory = false;
		bool selected = false;
		bool enabled = true;
		InteractionState interaction = InteractionState::Normal;
	};
} // namespace fyuu_ui
