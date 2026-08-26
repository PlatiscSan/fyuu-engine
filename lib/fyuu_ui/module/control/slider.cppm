export module fyuu_ui:control_slider;
import :control_common;
export namespace fyuu_ui {
	/// A bounded value control. A zero step leaves values continuous; positive
	/// steps request host-side quantization relative to `minimum`.
	struct Slider {
		float minimum = 0.0f;
		float maximum = 1.0f;
		float value = 0.0f;
		float step = 0.0f;
		Orientation orientation = Orientation::Horizontal;
		InteractionState interaction = InteractionState::Normal;
	};
} // namespace fyuu_ui
