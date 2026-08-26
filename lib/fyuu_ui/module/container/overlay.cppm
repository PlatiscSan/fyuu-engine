export module fyuu_ui:container_overlay;

export namespace fyuu_ui {
	/// Arranges every child in the same available rectangle, in insertion order.
	struct Overlay {
		bool clip_to_bounds = false;
	};
} // namespace fyuu_ui
