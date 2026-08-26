module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#endif

export module fyuu_ui:control_common;
#if defined(__cpp_lib_modules)
import std;
#endif

export namespace fyuu_ui {
	/// Selects the primary axis used by linear layout and value controls.
	enum class Orientation : std::uint8_t { Horizontal, Vertical };
	/// Pointer-driven visual state; focus, selection and enabled state are separate.
	enum class InteractionState : std::uint8_t { Normal, Hovered, Pressed };
} // namespace fyuu_ui
