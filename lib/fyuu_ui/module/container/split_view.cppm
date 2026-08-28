export module fyuu_ui:container_split_view;

import :control_common;

export namespace fyuu_ui {
	/// Divides its first two children along one axis. `split` is the normalized
	/// first-pane share before minimum extents and divider spacing are applied.
	struct SplitView {
		Orientation orientation = Orientation::Horizontal;
		float split = 0.5f;
		float minimum_first = 0.0f;
		float minimum_second = 0.0f;
		float spacing = 0.0f;
		bool resizable = true;
		InteractionState interaction = InteractionState::Normal;
	};
} // namespace fyuu_ui
