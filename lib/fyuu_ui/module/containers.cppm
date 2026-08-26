module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <variant>
#endif

export module fyuu_ui:containers;
#if defined(__cpp_lib_modules)
import std;
#endif

export import :container_overlay;
export import :container_window_layer;
export import :container_stack_panel;
export import :container_split_view;
export import :container_scroll_view;

export namespace fyuu_ui {
	/// Closed set of parent-capable layout values owned by logical nodes.
	using Container = std::variant<Overlay, WindowLayer, StackPanel, SplitView, ScrollView>;
}
