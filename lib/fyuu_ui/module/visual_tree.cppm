module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <variant>
#endif // !defined(__cpp_lib_modules)

export module fyuu_ui:visual_tree;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :geometry;
import :pass_key;

export namespace fyuu_ui {

	class LogicalTree;

	enum class HitTestRole : std::uint8_t {
		Content,
		MenuContent,
		WindowNonClient,
		WindowNonClientButton,
		WindowResize
	};

	enum class WindowResizeRegion : std::uint8_t {
		None,
		Left,
		Top,
		Right,
		Bottom,
		TopLeft,
		TopRight,
		BottomLeft,
		BottomRight
	};

	struct RectangleVisual {
		Rect bounds;
		Color fill;
		HitTestRole hit_test_role = HitTestRole::Content;
	};

	struct GradientRectangleVisual {
		Rect bounds;
		Color top;
		Color bottom;
		HitTestRole hit_test_role = HitTestRole::Content;
	};

	struct LineVisual {
		Point start;
		Point end;
		Color color;
		float thickness = 1.0f;
	};

	/// Identifies one menu entry by its index path from the menu bar root.
	/// `{ 1 }` is the second bar item; `{ 1, 0 }` is the first item in its dropdown.
	struct MenuPath {
		std::vector<std::uint32_t> indices;
	};

	struct HitTestVisual {
		Rect bounds;
		HitTestRole role = HitTestRole::Content;
		WindowResizeRegion resize_region = WindowResizeRegion::None;
		std::optional<MenuPath> menu_path;
	};

	struct TextVisual {
		Rect bounds;
		std::string text;
		Color color;
		float font_size = 14.0f;
		std::optional<std::size_t> caret_offset;
	};

	struct ClipVisual {
		Rect bounds;
	};

	struct TransformVisual {
		Transform2D transform;
	};

	using Visual = std::variant<
		RectangleVisual,
		GradientRectangleVisual,
		LineVisual,
		HitTestVisual,
		TextVisual,
		ClipVisual,
		TransformVisual
	>;

	struct PushTransformCommand {
		Transform2D transform;
	};

	struct PopTransformCommand {};

	struct PushClipCommand {
		Rect bounds;
	};

	struct PopClipCommand {};

	struct DrawRectangleCommand {
		Rect bounds;
		Color fill;
	};

	struct DrawGradientRectangleCommand {
		Rect bounds;
		Color top;
		Color bottom;
	};

	struct DrawLineCommand {
		Point start;
		Point end;
		Color color;
		float thickness = 1.0f;
	};

	struct DrawTextCommand {
		Rect bounds;
		std::string text;
		Color color;
		float font_size = 14.0f;
		std::optional<std::size_t> caret_offset;
	};

	using DrawCommand = std::variant<
		PushTransformCommand,
		PopTransformCommand,
		PushClipCommand,
		PopClipCommand,
		DrawRectangleCommand,
		DrawGradientRectangleCommand,
		DrawLineCommand,
		DrawTextCommand
	>;

	struct HitTestResult {
		std::uint64_t logical_id;
		Point position;
		Size size;
		HitTestRole role = HitTestRole::Content;
		WindowResizeRegion resize_region = WindowResizeRegion::None;
		std::optional<MenuPath> menu_path;
	};

	/// Owns one immutable visual graph. Nodes are append-only during construction,
	/// so their IDs are direct indices into one contiguous value array.
	class VisualTree final {
	private:
		struct NodeState {
			Visual visual;
			std::uint64_t logical_id;
			std::optional<std::uint64_t> parent;
			std::optional<std::uint64_t> child;
			std::optional<std::uint64_t> sibling;
		};

		std::vector<NodeState> m_nodes;

	public:
		explicit VisualTree(PassKey<LogicalTree>) noexcept;
		~VisualTree() noexcept = default;
		VisualTree(VisualTree const&) = delete;
		VisualTree& operator=(VisualTree const&) = delete;
		VisualTree(VisualTree&&) noexcept = default;
		VisualTree& operator=(VisualTree&&) noexcept = default;

		std::uint64_t Insert(PassKey<LogicalTree>, std::uint64_t logical_id, Visual const& visual);
		void AddChild(PassKey<LogicalTree>, std::uint64_t parent_id, std::uint64_t child_id);

		/// Returns a backend-independent display list.
		/// Call chain: VisualTree traversal -> scoped transform and clip commands
		/// -> primitive draw commands -> Studio UI renderer -> FyuuRHI.
		std::vector<DrawCommand> WriteDrawList() const;

		/// Returns the topmost logical node painted at point. The iterative test
		/// applies the same transform and clip hierarchy as WriteDrawList.
		std::optional<HitTestResult> HitTest(Point point) const;
	};

}
