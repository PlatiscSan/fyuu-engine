export module fyuu_ui:container_window_layer;

export namespace fyuu_ui {
	/// Hosts top-level Window widgets. Child insertion order is also the window
	/// stacking order; LogicalNode::BringToFront moves a window to the top.
	struct WindowLayer {};
} // namespace fyuu_ui
