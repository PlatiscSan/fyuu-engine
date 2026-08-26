module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#endif // !defined(__cpp_lib_modules)

export module fyuu_ui:geometry;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_ui {

	/// A position or translation in logical (DPI-independent) pixels.
	struct Point {
		float x = 0.0f;
		float y = 0.0f;
	};

	/// Non-negative logical extents produced and consumed by layout.
	struct Size {
		float width = 0.0f;
		float height = 0.0f;
	};

	/// An axis-aligned logical-pixel rectangle with a top-left origin.
	struct Rect {
		Point position;
		Size size;
	};

	/// Stores both the measure result and final arranged rectangle for one node.
	struct LayoutResult {
		Size desired_size;
		Rect bounds;
	};

	/// Linear RGBA components in the normalized [0, 1] range.
	struct Color {
		float red = 0.0f;
		float green = 0.0f;
		float blue = 0.0f;
		float alpha = 1.0f;
	};

	/// The translation subset currently supported by VisualTree transforms.
	struct Transform2D {
		Point translation;
	};

}
