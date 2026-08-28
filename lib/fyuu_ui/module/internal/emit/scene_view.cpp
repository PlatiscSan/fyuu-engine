module fyuu_ui:emit_scene_view_impl;
import :emitter;
namespace fyuu_ui::detail {
	// SceneView identifies the application-owned texture surface without coupling
	// FyuuUI to an asset or graphics backend.
	EmitterOutput Emit(Rect bounds, StyleOverride const& local_style, OptionalColor foreground, OptionalFloat font_size, Theme const& theme, TextMeasurer const& measure_text, bool has_two_children, Size first_child_size, SceneView const& value) {
		EmitterOutput output;
		output.Append(SceneTextureVisual{
			bounds,
			local_style.background.value_or(value.clear_color)
		});
		return output;
	}
} // namespace fyuu_ui::detail
