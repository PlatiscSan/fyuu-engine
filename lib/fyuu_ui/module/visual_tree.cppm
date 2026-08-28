module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <vector>
#include <string>
#include <cstdint>
#include <optional>
#include <variant>
#include <span>
#endif // !defined(__cpp_lib_modules)

export module fyuu_ui:visual_tree;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :geometry;
import :pass_key;

export namespace fyuu_ui {

	class LogicalTree;

	/// Distinguishes content hits from window chrome whose gesture semantics are
	/// interpreted by the host rather than by the logical control itself.
	enum class HitTestRole : std::uint8_t {
		Content,
		MenuContent,
		WindowNonClient,
		WindowNonClientButton,
		WindowResize
	};

	/// Identifies the edge/corner associated with a WindowResize hit visual.
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

	/// A solid rectangle local to its visual-tree parent transform.
	struct RectangleVisual {
		Rect bounds;
		Color fill;
		HitTestRole hit_test_role = HitTestRole::Content;
	};

	/// Marks the application-owned texture surface reserved by SceneView.
	struct SceneTextureVisual {
		Rect bounds;
		Color fallback;
	};

	/// A vertical two-stop gradient rectangle.
	struct GradientRectangleVisual {
		Rect bounds;
		Color top;
		Color bottom;
		HitTestRole hit_test_role = HitTestRole::Content;
	};

	/// A logical-pixel line segment with explicit thickness.
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

	/// Invisible geometry used when interactive bounds differ from painted bounds.
	struct HitTestVisual {
		Rect bounds;
		HitTestRole role = HitTestRole::Content;
		WindowResizeRegion resize_region = WindowResizeRegion::None;
		MenuPath menu_path;
	};

	/// Renderer-independent single-line text including optional editor decorations.
	/// Selection/caret values are UTF-8 byte offsets into `text`.
	struct TextVisual {
		Rect bounds;
		std::string text;
		Color color;
		float font_size = 14.0f;
		std::optional<std::size_t> caret_offset;
		std::optional<std::size_t> selection_start;
		std::optional<std::size_t> selection_end;
		float horizontal_offset = 0.0f;
		Color selection_color{};
	};

	/// Clips all descendants to bounds until visual traversal leaves this node.
	struct ClipVisual {
		Rect bounds;
	};

	/// Applies a transform to descendants; the current transform model is translation-only.
	struct TransformVisual {
		Transform2D transform;
	};

	/// Closed set of retained visual primitives emitted by FyuuUI controls.
	using Visual = std::variant<
		RectangleVisual,
		SceneTextureVisual,
		GradientRectangleVisual,
		LineVisual,
		HitTestVisual,
		TextVisual,
		ClipVisual,
		TransformVisual
	>;

	/// Draw-list commands form a balanced stack stream consumed in order.
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

	struct DrawSceneTextureCommand {
		Rect bounds;
		Color fallback;
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

	/// Flattened text payload retaining edit decorations for the renderer.
	struct DrawTextCommand {
		Rect bounds;
		std::string text;
		Color color;
		float font_size = 14.0f;
		std::optional<std::size_t> caret_offset;
		std::optional<std::size_t> selection_start;
		std::optional<std::size_t> selection_end;
		float horizontal_offset = 0.0f;
		Color selection_color{};
	};

	/// Immediate command stream produced from the retained VisualTree.
	using DrawCommand = std::variant<
		PushTransformCommand,
		PopTransformCommand,
		PushClipCommand,
		PopClipCommand,
		DrawRectangleCommand,
		DrawSceneTextureCommand,
		DrawGradientRectangleCommand,
		DrawLineCommand,
		DrawTextCommand
	>;

	/// Topmost hit expressed in both logical-node identity and local hit coordinates.
	struct HitTestResult {
		std::uint64_t logical_id;
		Point position;
		Size size;
		HitTestRole role = HitTestRole::Content;
		WindowResizeRegion resize_region = WindowResizeRegion::None;
		MenuPath menu_path;
	};

	namespace detail {
		inline constexpr auto DetachedVisualNode = ~std::uint64_t{0u};
		/// Retained storage exposed read-only to the draw-list and hit-test processors.
		struct VisualNode {
			Visual visual;
			std::uint64_t logical_id;
			std::uint64_t parent = DetachedVisualNode;
			std::uint64_t child = 0u;
			std::uint64_t sibling = 0u;
		};
	}

	/// Retained visual hierarchy built afresh from a LogicalTree. Nodes are
	/// append-only during construction, so IDs directly index contiguous storage.
	/// It owns no GPU objects and supports both rendering and topmost hit testing.
	class VisualTree final {
	private:
		std::vector<detail::VisualNode> m_nodes;

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
