module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#endif
export module fyuu_ui:control_numeric_box;
#if defined(__cpp_lib_modules)
import std;
#endif
import :control_common;
export namespace fyuu_ui {
	/// A bounded scalar editor. Hosts apply keyboard, wheel and drag gestures and
	/// raise ValueChangedEvent after changing `value`.
	struct NumericBox {
		double minimum = 0.0;
		double maximum = 100.0;
		double value = 0.0;
		double step = 1.0;
		std::uint32_t decimal_places = 0u;
		bool read_only = false;
		bool focused = false;
		InteractionState interaction = InteractionState::Normal;
	};
} // namespace fyuu_ui
