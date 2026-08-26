export module fyuu_ui:container_stack_panel;

import :control_common;

export namespace fyuu_ui {
	/// Measures and arranges children consecutively along `orientation`.
	struct StackPanel {
		Orientation orientation = Orientation::Vertical;
		float spacing = 0.0f;
	};
} // namespace fyuu_ui
