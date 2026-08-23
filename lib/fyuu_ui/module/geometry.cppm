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

	struct Point {
		float x = 0.0f;
		float y = 0.0f;
	};

	struct Size {
		float width = 0.0f;
		float height = 0.0f;
	};

	struct Rect {
		Point position;
		Size size;
	};

	struct LayoutResult {
		Size desired_size;
		Rect bounds;
	};

	struct Color {
		float red = 0.0f;
		float green = 0.0f;
		float blue = 0.0f;
		float alpha = 1.0f;
	};

	struct Transform2D {
		Point translation;
	};

}
