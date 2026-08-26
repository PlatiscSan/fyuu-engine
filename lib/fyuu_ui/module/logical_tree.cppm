module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>
#include <algorithm>
#include <functional>
#include <string>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <optional>
#include <variant>
#include <string_view>
#include <concepts>
#include <span>
#endif // !defined(__cpp_lib_modules)

export module fyuu_ui:logical_tree;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :pass_key;
import :geometry;
import :theme;
import :visual_tree;
export import :controls;
export import :containers;

export namespace fyuu_ui {
	/// Measures text in logical pixels. The host supplies this callback so layout
	/// uses the exact font selection, DPI scale, and glyph advances used to render.
	using TextMeasurer = std::function<Size(std::string_view text, float font_size)>;

	class LogicalNode;
	class LogicalTree;
	class EventBus;

	/// Selects the direction in which an event visits the logical hierarchy.
	enum class RoutingStrategy : std::uint8_t {
		Direct = 1u << 0u,
		Tunnel = 1u << 1u,
		Bubble = 1u << 2u
	};

	constexpr RoutingStrategy operator|(RoutingStrategy left, RoutingStrategy right) noexcept {
		return static_cast<RoutingStrategy>(
		    static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right)
		);
	}

	constexpr bool operator&(RoutingStrategy left, RoutingStrategy right) noexcept {
		return (static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right)) != 0u;
	}

	/// Semantic primary-button activation raised after a completed press/release.
	struct ClickEvent {
		static constexpr std::uint64_t ID = 1u;
		static constexpr RoutingStrategy Routing = RoutingStrategy::Bubble;

		Point position;
		bool handled = false;
	};

	/// Raised by the host when a leaf menu entry is activated. The host dispatches
	/// it on the MenuBar node; subscribers receive the root-to-item path.
	struct MenuActivatedEvent {
		static constexpr std::uint64_t ID = 2u;
		static constexpr RoutingStrategy Routing = RoutingStrategy::Bubble;

		MenuPath path;
		bool handled = false;
	};

	/// Platform-independent pointer button identity used by routed input.
	enum class PointerButton : std::uint8_t { None, Left, Middle, Right, Extra1, Extra2 };
	/// Platform-independent keys currently interpreted by built-in FyuuUI behavior.
	enum class Key : std::uint8_t {
		Unknown,
		Tab,
		LeftArrow,
		RightArrow,
		UpArrow,
		DownArrow,
		PageUp,
		PageDown,
		Home,
		End,
		Insert,
		Delete,
		Backspace,
		Space,
		Enter,
		Escape,
		A,
		C,
		V,
		X,
		Y,
		Z
	};

	/// Pointer press routed through ancestors in tunnel then bubble order.
	struct PointerPressedEvent {
		static constexpr std::uint64_t ID = 3u;
		static constexpr RoutingStrategy Routing =
		    RoutingStrategy::Tunnel | RoutingStrategy::Bubble;
		Point position;
		PointerButton button = PointerButton::None;
		std::uint8_t click_count = 0u;
		bool handled = false;
	};

	/// Pointer motion routed to capture when present, otherwise to the hit subtree.
	struct PointerMovedEvent {
		static constexpr std::uint64_t ID = 4u;
		static constexpr RoutingStrategy Routing =
		    RoutingStrategy::Tunnel | RoutingStrategy::Bubble;
		Point position;
		bool handled = false;
	};

	/// Pointer release paired with the button that ended the gesture.
	struct PointerReleasedEvent {
		static constexpr std::uint64_t ID = 5u;
		static constexpr RoutingStrategy Routing =
		    RoutingStrategy::Tunnel | RoutingStrategy::Bubble;
		Point position;
		PointerButton button = PointerButton::None;
		bool handled = false;
	};

	/// Physical key transition with modifier state; repeated presses remain host policy.
	struct KeyDownEvent {
		static constexpr std::uint64_t ID = 6u;
		static constexpr RoutingStrategy Routing =
		    RoutingStrategy::Tunnel | RoutingStrategy::Bubble;
		Key key = Key::Unknown;
		bool shift = false;
		bool control = false;
		bool alt = false;
		bool handled = false;
	};

	/// Physical key release routed through the current focus scope.
	struct KeyUpEvent {
		static constexpr std::uint64_t ID = 7u;
		static constexpr RoutingStrategy Routing =
		    RoutingStrategy::Tunnel | RoutingStrategy::Bubble;
		Key key = Key::Unknown;
		bool shift = false;
		bool control = false;
		bool alt = false;
		bool handled = false;
	};

	/// Committed UTF-8 text from the platform input method, separate from KeyDown.
	struct TextInputEvent {
		static constexpr std::uint64_t ID = 8u;
		static constexpr RoutingStrategy Routing = RoutingStrategy::Bubble;
		std::string text;
		bool handled = false;
	};

	/// Direct notification raised after the stored and control focus states agree.
	struct FocusChangedEvent {
		static constexpr std::uint64_t ID = 9u;
		static constexpr RoutingStrategy Routing = RoutingStrategy::Direct;
		bool focused = false;
		bool handled = false;
	};

	/// Value payload shared by built-in Boolean, scalar and text editors.
	using ControlValue = std::variant<bool, float, double, std::string>;

	/// Bubbling notification containing values before and after one logical edit.
	struct ValueChangedEvent {
		static constexpr std::uint64_t ID = 10u;
		static constexpr RoutingStrategy Routing = RoutingStrategy::Bubble;
		ControlValue previous;
		ControlValue current;
		bool handled = false;
	};

	/// Raised when a TextBox edit transaction is accepted, normally by Enter.
	struct TextSubmittedEvent {
		static constexpr std::uint64_t ID = 11u;
		static constexpr RoutingStrategy Routing = RoutingStrategy::Bubble;
		std::string text;
		bool handled = false;
	};

	/// Raised after a TextBox restores its edit snapshot, normally by Escape.
	struct TextCancelledEvent {
		static constexpr std::uint64_t ID = 12u;
		static constexpr RoutingStrategy Routing = RoutingStrategy::Bubble;
		std::string restored_text;
		bool handled = false;
	};

	/// A routed event is a value type with a stable ID, declared routing
	/// strategies, and mutable handled state. No event inheritance is required.
	template <class Event>
	concept RoutedEvent = requires(Event event) {
		{ Event::ID } -> std::convertible_to<std::uint64_t>;
		{ Event::Routing } -> std::convertible_to<RoutingStrategy>;
		requires std::same_as<decltype((event.handled)), bool&>;
	};

	/// Placement within the space assigned by a parent container.
	enum class Alignment : std::uint8_t { Start, Center, End, Stretch };

	/// Independent logical-pixel extents ordered left, top, right, bottom.
	struct Thickness {
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
	};

	/// Internal absolute placement used by WindowLayer.
	struct WindowLayerLayout {
		std::optional<float> left;
		std::optional<float> top;
		std::optional<float> right;
		std::optional<float> bottom;
	};

	/// Stores common layout constraints and parent-container-specific attached
	/// state. The logical tree owns this value alongside the node content.
	struct LayoutProperties {
		Thickness margin;
		std::optional<float> width;
		std::optional<float> height;
		float minimum_width = 0.0f;
		float minimum_height = 0.0f;
		std::optional<float> maximum_width;
		std::optional<float> maximum_height;
		Alignment horizontal_alignment = Alignment::Stretch;
		Alignment vertical_alignment = Alignment::Stretch;
		WindowLayerLayout window;
	};

	/// Owns one rooted logical hierarchy and every event handler subscribed to
	/// its nodes. LogicalNode never owns either node state or handler state.
	class LogicalTree final {
	private:
		static constexpr std::uint64_t DetachedNode = ~std::uint64_t{0u};
		/// Type-erased handler storage owned by the tree. Entries are addressed by a
		/// stable subscription ID, while m_index narrows dispatch directly to
		/// node/event matches instead of scanning every handler in the application.
		/// One stable slot in m_nodes. Hierarchy is represented as a first-child /
		/// next-sibling intrusive list, avoiding per-node child allocations.
		struct NodeState {
			std::variant<Container, Widget> content;
			LayoutProperties layout;
			StyleOverride style;
			std::uint64_t parent = DetachedNode;
			std::uint64_t child = 0u;
			std::uint64_t sibling = 0u;
		};

		// IDs are vector indices. Removing a node empties its slot; InsertNode may
		// reuse that slot, but live nodes are never moved or renumbered.
		std::vector<std::optional<NodeState>> m_nodes;
		std::uint64_t m_window_layer_id = 0u; // node ID + 1; zero means absent

		/// Allocates a detached node. AddChild is the only operation that links it
		/// into the hierarchy and establishes its parent/sibling invariants.
		LogicalNode InsertNode(std::variant<Container, Widget> const& content);
		/// Makes exactly one direct child of a WindowLayer active.
		void ActivateWindow(std::uint64_t layer_id, std::uint64_t window_id) noexcept;
		/// Emits the arranged logical hierarchy into a value-owned visual tree.
		VisualTree EmitVisualTree(
		    std::span<std::uint64_t const> order,
		    std::span<LayoutResult const> layout,
		    std::span<std::optional<Color> const> foregrounds,
		    std::span<std::optional<float> const> font_sizes,
		    Theme const& theme,
		    TextMeasurer const& measure_text
		) const;

	public:
		explicit LogicalTree(Container const& root);
		~LogicalTree() noexcept = default;
		LogicalTree(LogicalTree const&) = delete;
		LogicalTree& operator=(LogicalTree const&) = delete;
		LogicalTree(LogicalTree&&) noexcept = default;
		LogicalTree& operator=(LogicalTree&&) noexcept = default;

		/// EventBus receives only these guarded projections; NodeState and the tree's
		/// ownership containers remain inaccessible outside LogicalTree.
		[[nodiscard]] bool IsAttached(
		    PassKey<EventBus>,
		    std::uint64_t node_id
		) const noexcept;
		[[nodiscard]] bool IsFocusable(
		    PassKey<EventBus>,
		    std::uint64_t node_id
		) const noexcept;
		[[nodiscard]] std::vector<std::uint64_t> BuildFocusOrder(
		    PassKey<EventBus>,
		    std::uint64_t root_id
		) const;
		void SetFocused(PassKey<EventBus>, std::uint64_t node_id, bool focused) noexcept;

		/// Attaches an existing detached node at the end of the parent's child list.
		void AddChild(PassKey<LogicalNode>, LogicalNode const* parent, LogicalNode const& child);
		void SetLayout(PassKey<LogicalNode>, std::uint64_t id, LayoutProperties const& layout);
		void SetStyle(PassKey<LogicalNode>, std::uint64_t id, StyleOverride const& style);
		/// Moves a node to the tail of its sibling list. For WindowLayer this is also
		/// the active/topmost window transition.
		void BringToFront(PassKey<LogicalNode>, std::uint64_t id);
		/// Container insertion is guarded because a tree owns at most one WindowLayer.
		LogicalNode Insert(PassKey<LogicalNode>, Container const& container);
		LogicalNode Insert(PassKey<LogicalNode>, Widget const& widget);

		template <class WidgetType>
		WidgetType& AsWidget(PassKey<LogicalNode>, std::uint64_t node_id) {
			return std::get<WidgetType>(std::get<Widget>(m_nodes[node_id]->content));
		}

		template <class ContainerType>
		ContainerType& AsContainer(PassKey<LogicalNode>, std::uint64_t node_id) {
			return std::get<ContainerType>(std::get<Container>(m_nodes[node_id]->content));
		}

		template <class ContainerType>
		ContainerType* TryAsContainer(PassKey<LogicalNode>, std::uint64_t node_id) noexcept {
			if (node_id >= m_nodes.size() || !m_nodes[node_id])
				return nullptr;
			auto* container = std::get_if<Container>(&m_nodes[node_id]->content);
			return container == nullptr ? nullptr : std::get_if<ContainerType>(container);
		}

		/// Returns a non-owning handle to permanent node zero.
		LogicalNode GetRoot() noexcept;
		/// Removes a node and its complete subtree. Removing the root or a missing ID
		/// is a no-op; nodes returned by Insert but not yet attached are also supported.
		void Remove(std::uint64_t id) noexcept;
		/// Returns a non-owning handle, or throws if the stable slot is empty/invalid.
		LogicalNode GetNode(std::uint64_t id);
		/// Returns the unique WindowLayer or throws when the tree has none.
		LogicalNode GetWindowLayer();
		/// Reports whether node_id is root_id itself or one of its attached descendants.
		[[nodiscard]] bool IsInSubtree(std::uint64_t root_id, std::uint64_t node_id) const noexcept;
		/// Creates an event bus with independent handler ownership. The bus requests
		/// current routes from this tree when dispatching, so it stores no tree reference.
		[[nodiscard]] EventBus BuildEventBus() const;
		[[nodiscard]] std::vector<std::uint64_t> CollectSubtree(
		    PassKey<EventBus>,
		    std::uint64_t root_id
		) const {
			if (root_id >= m_nodes.size() || !m_nodes[root_id])
				return {};
			std::vector<std::uint64_t> result{root_id};
			for (std::size_t index = 0u; index < result.size(); ++index) {
				for (
				    auto child = m_nodes[result[index]]->child; child != 0u;
				    child = m_nodes[child]->sibling
				)
					result.emplace_back(child);
			}
			return result;
		}
		/// Returns source-to-root IDs, including source and the permanent root.
		[[nodiscard]] std::vector<std::uint64_t> Ancestors(std::uint64_t source_id) const;

		/// Builds a value-owned visual snapshot for the requested viewport.
		/// Call chain: flatten the logical hierarchy -> measure from leaves to root
		/// -> arrange from root to leaves -> emit visual nodes in painting order.
		/// Every traversal is iterative; this function never follows the hierarchy
		/// through recursive calls.
		[[nodiscard]] VisualTree BuildVisualTree(
		    Size const& available_size,
		    Theme const& theme,
		    TextMeasurer const& measure_text
		);
	};

	/// Identifies one node owned by a LogicalTree. The handle remains separate
	/// from Widget and Container; all node state remains owned by LogicalTree.
	class LogicalNode final {
	private:
		friend class LogicalTree;
		LogicalTree* m_tree;
		std::uint64_t m_id;

	public:
		/// Internal construction only: PassKey prevents callers from fabricating a
		/// handle with a tree/ID pair that the tree did not validate.
		LogicalNode(PassKey<LogicalTree>, LogicalTree* tree, std::uint64_t id) noexcept :
		    m_tree(tree), m_id(id) {
		}

		LogicalNode(LogicalNode const&) noexcept = default;
		LogicalNode& operator=(LogicalNode const&) noexcept = default;
		LogicalNode(LogicalNode&&) noexcept = default;
		LogicalNode& operator=(LogicalNode&&) noexcept = default;

		/// Allocates, attaches, and returns a handle to a new last child.
		LogicalNode AddChild(Widget const& widget);
		/// Allocates, attaches, and returns a handle to a new last child.
		LogicalNode AddChild(Container const& container);

		/// Replaces this node's layout state. Call chain: LogicalNode -> guarded
		/// LogicalTree mutation -> BuildVisualTree measure and arrange passes.
		void SetLayout(LayoutProperties const& layout);

		/// Replaces this node's local style overrides. Foreground and font size
		/// propagate during BuildVisualTree; all other properties remain local.
		void SetStyle(StyleOverride const& style);

		/// Moves this node to the end of its parent's sibling chain. WindowLayer
		/// paints later siblings above earlier siblings, so this raises a window.
		void BringToFront();

		template <class WidgetType> WidgetType& AsWidget() {
			return m_tree->AsWidget<WidgetType>(PassKey<LogicalNode>{}, m_id);
		}

		template <class ContainerType> ContainerType& AsContainer() {
			return m_tree->AsContainer<ContainerType>(PassKey<LogicalNode>{}, m_id);
		}

		template <class ContainerType> ContainerType* TryAsContainer() noexcept {
			return m_tree->TryAsContainer<ContainerType>(PassKey<LogicalNode>{}, m_id);
		}

		std::uint64_t GetID() const noexcept {
			return m_id;
		}
	};

} // namespace fyuu_ui
