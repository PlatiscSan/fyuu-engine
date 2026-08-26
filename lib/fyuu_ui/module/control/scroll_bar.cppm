export module fyuu_ui:control_scroll_bar;
import :control_common;

export namespace fyuu_ui {
	/// Stateful scrollbar content embedded by a scrolling container. The owner
	/// supplies its viewport-derived geometry; ScrollBar stores only interaction.
	struct ScrollBar {
		InteractionState interaction = InteractionState::Normal;
		bool dragging = false;
		float track_origin = 0.0f;
		float grab_offset = 0.0f;
	};
} // namespace fyuu_ui
