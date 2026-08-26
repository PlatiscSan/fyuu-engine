module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>
#endif
export module fyuu_ui:control_text_block;
#if defined(__cpp_lib_modules)
import std;
#endif
import :geometry;
export namespace fyuu_ui {
	/// Displays immutable-from-the-user text using explicit color and font size.
	struct TextBlock {
		std::string text;
		Color color;
		float font_size = 14.0f;
	};
} // namespace fyuu_ui
