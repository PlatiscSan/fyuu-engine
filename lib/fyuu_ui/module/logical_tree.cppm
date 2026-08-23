module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <vector>
#include <algorithm>
#include <functional>
#include <string>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <optional>
#include <variant>
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

export namespace fyuu_ui {

	class LogicalNode;
	class LogicalTree;

	/// Selects the direction in which an event visits the logical hierarchy.
	enum class RoutingStrategy : std::uint8_t {
		Direct = 1u << 0u,
		Tunnel = 1u << 1u,
		Bubble = 1u << 2u
	};

	constexpr RoutingStrategy operator|(RoutingStrategy left, RoutingStrategy right) noexcept {
		return static_cast<RoutingStrategy>(
			static_cast<std::uint8_t>(left) |
			static_cast<std::uint8_t>(right)
		);
	}

	constexpr bool operator&(RoutingStrategy left, RoutingStrategy right) noexcept {
		return (static_cast<std::uint8_t>(left) & static_cast<std::uint8_t>(right)) != 0u;
	}

	struct ClickEvent {
		static constexpr std::uint64_t ID = 1u;
		static constexpr RoutingStrategy Routing = RoutingStrategy::Bubble;

		Point position;
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

	enum class Orientation : std::uint8_t {
		Horizontal,
		Vertical
	};

	enum class Alignment : std::uint8_t {
		Start,
		Center,
		End,
		Stretch
	};

	enum class Dock : std::uint8_t {
		Left,
		Top,
		Right,
		Bottom
	};

	struct Thickness {
		float left = 0.0f;
		float top = 0.0f;
		float right = 0.0f;
		float bottom = 0.0f;
	};

	/// Attached state interpreted only when the logical parent is a Grid.
	struct GridLayout {
		std::uint32_t row = 0u;
		std::uint32_t column = 0u;
		std::uint32_t row_span = 1u;
		std::uint32_t column_span = 1u;
	};

	/// Attached state interpreted only when the logical parent is a Canvas.
	struct CanvasLayout {
		std::optional<float> left;
		std::optional<float> top;
		std::optional<float> right;
		std::optional<float> bottom;
	};

	/// Attached state interpreted only when the logical parent is a DockPanel.
	struct DockLayout {
		Dock dock = Dock::Left;
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
		GridLayout grid;
		CanvasLayout canvas;
		DockLayout dock;
	};

	struct Overlay {
		bool clip_to_bounds = false;
	};

	/// Hosts top-level Window widgets. Child insertion order is also the window
	/// stacking order; LogicalNode::BringToFront moves a window to the top.
	struct WindowLayer {};

	struct StackPanel {
		Orientation orientation = Orientation::Vertical;
		float spacing = 0.0f;
	};

	struct Grid {
		std::uint32_t rows = 1u;
		std::uint32_t columns = 1u;
		float row_spacing = 0.0f;
		float column_spacing = 0.0f;
	};

	struct Canvas {
		bool clip_to_bounds = false;
	};

	struct WrapPanel {
		Orientation orientation = Orientation::Horizontal;
		float horizontal_spacing = 0.0f;
		float vertical_spacing = 0.0f;
	};

	struct DockPanel {
		bool last_child_fill = true;
		float spacing = 0.0f;
	};

	struct UniformGrid {
		std::uint32_t rows = 0u;
		std::uint32_t columns = 0u;
		float horizontal_spacing = 0.0f;
		float vertical_spacing = 0.0f;
	};

	struct SplitView {
		Orientation orientation = Orientation::Horizontal;
		float split = 0.5f;
		float minimum_first = 0.0f;
		float minimum_second = 0.0f;
		float spacing = 0.0f;
		bool resizable = true;
	};

	using Container = std::variant<
		Overlay,
		WindowLayer,
		StackPanel,
		Grid,
		Canvas,
		WrapPanel,
		DockPanel,
		UniformGrid,
		SplitView
	>;

	struct Border {
		Color background;
	};

	struct Spacer {
		Size size;
	};

	struct TextBlock {
		std::string text;
		Color color;
		float font_size = 14.0f;
	};

	struct Separator {
		Color color;
		Orientation orientation = Orientation::Horizontal;
		float thickness = 1.0f;
	};

	struct ProgressBar {
		float minimum = 0.0f;
		float maximum = 1.0f;
		float value = 0.0f;
	};

	enum class InteractionState : std::uint8_t {
		Normal,
		Hovered,
		Pressed
	};

	struct Button {
		std::string title;
		bool enabled = true;
		bool default_button = false;
		InteractionState interaction = InteractionState::Normal;
	};

	struct ToggleButton {
		std::string title;
		bool checked = false;
		bool enabled = true;
		InteractionState interaction = InteractionState::Normal;
	};

	struct CheckBox {
		std::string title;
		bool checked = false;
		bool enabled = true;
		InteractionState interaction = InteractionState::Normal;
	};

	struct RadioButton {
		std::string title;
		bool checked = false;
		bool enabled = true;
		InteractionState interaction = InteractionState::Normal;
	};

	struct Slider {
		float minimum = 0.0f;
		float maximum = 1.0f;
		float value = 0.0f;
		float step = 0.0f;
		Orientation orientation = Orientation::Horizontal;
		InteractionState interaction = InteractionState::Normal;
	};

	struct TextBox {
		std::string text;
		std::string placeholder;
		bool read_only = false;
		bool focused = false;
		std::size_t caret_offset = 0u;
	};

	struct SearchBox {
		std::string text;
		std::string placeholder;
		bool focused = false;
		std::size_t caret_offset = 0u;
	};

	struct NumericBox {
		double minimum = 0.0;
		double maximum = 100.0;
		double value = 0.0;
		double step = 1.0;
		std::uint32_t decimal_places = 0u;
		bool read_only = false;
		bool focused = false;
		InteractionState interaction = InteractionState::Normal;
	};

	struct MenuItem {
		std::string title;
		bool enabled = true;
		bool checked = false;
		InteractionState interaction = InteractionState::Normal;
	};

	struct SceneView {
		Color clear_color;
	};

	/// Stores the state needed to arrange and draw one top-level UI window.
	/// Its first logical child is arranged inside the area below the title bar.
	struct Window {
		std::string title;
		Point position;
		Size size;
		bool closable = true;
		InteractionState non_client_button_interaction = InteractionState::Normal;
		Size minimum_size{ 160.0f, 96.0f };
		bool resizable = true;
		bool active = false;
	};

	using Widget = std::variant<
		Border,
		Spacer,
		TextBlock,
		Separator,
		ProgressBar,
		Button,
		ToggleButton,
		CheckBox,
		RadioButton,
		Slider,
		TextBox,
		SearchBox,
		NumericBox,
		MenuItem,
		SceneView,
		Window
	>;

	/// Owns one rooted logical hierarchy and every event handler subscribed to
	/// its nodes. LogicalNode never owns either node state or handler state.
	class LogicalTree final {
	private:
		class EventStorage final {
		private:
			class Entry final {
			private:
				std::uint64_t m_node_id;
				std::uint64_t m_subscription_id;
				std::uint64_t m_event_id;
				RoutingStrategy m_routes;
				bool m_handled_events_too;
				void* m_handler;
				void (*m_invoke)(void*, void*);
				void (*m_destroy)(void*) noexcept;

			public:
				Entry(
					std::uint64_t node_id,
					std::uint64_t subscription_id,
					std::uint64_t event_id,
					RoutingStrategy routes,
					bool handled_events_too,
					void* handler,
					void (*invoke)(void*, void*),
					void (*destroy)(void*) noexcept
				) noexcept
					: m_node_id(node_id),
					m_subscription_id(subscription_id),
					m_event_id(event_id),
					m_routes(routes),
					m_handled_events_too(handled_events_too),
					m_handler(handler),
					m_invoke(invoke),
					m_destroy(destroy) {
				}

				~Entry() noexcept {
					if (m_destroy != nullptr) {
						m_destroy(m_handler);
					}
				}

				Entry(Entry const&) = delete;
				Entry& operator=(Entry const&) = delete;
				Entry(Entry&& other) noexcept
					: m_node_id(other.m_node_id),
					m_subscription_id(other.m_subscription_id),
					m_event_id(other.m_event_id),
					m_routes(other.m_routes),
					m_handled_events_too(other.m_handled_events_too),
					m_handler(std::exchange(other.m_handler, nullptr)),
					m_invoke(other.m_invoke),
					m_destroy(std::exchange(other.m_destroy, nullptr)) {
				}

				Entry& operator=(Entry&& other) noexcept {
					if (m_destroy != nullptr) {
						m_destroy(m_handler);
					}
					m_node_id = other.m_node_id;
					m_subscription_id = other.m_subscription_id;
					m_event_id = other.m_event_id;
					m_routes = other.m_routes;
					m_handled_events_too = other.m_handled_events_too;
					m_handler = std::exchange(other.m_handler, nullptr);
					m_invoke = other.m_invoke;
					m_destroy = std::exchange(other.m_destroy, nullptr);
					return *this;
				}

				std::uint64_t GetNodeID() const noexcept {
					return m_node_id;
				}

				std::uint64_t GetSubscriptionID() const noexcept {
					return m_subscription_id;
				}

				bool Matches(
					std::uint64_t node_id,
					std::uint64_t event_id,
					RoutingStrategy route,
					bool handled
				) const noexcept {
					return m_node_id == node_id &&
						m_event_id == event_id &&
						(m_routes & route) &&
						(!handled || m_handled_events_too);
				}

				void Invoke(void* event) {
					m_invoke(m_handler, event);
				}
			};

			std::vector<Entry> m_entries;
			std::uint64_t m_next_subscription_id = 0u;

		public:
			EventStorage() = default;
			~EventStorage() noexcept = default;
			EventStorage(EventStorage const&) = delete;
			EventStorage& operator=(EventStorage const&) = delete;
			EventStorage(EventStorage&&) noexcept = default;
			EventStorage& operator=(EventStorage&&) noexcept = default;

			template <RoutedEvent Event, class Handler>
			std::uint64_t Subscribe(std::uint64_t node_id, Handler&& handler, RoutingStrategy routes, bool handled_events_too) {
				using HandlerType = std::decay_t<Handler>;
				auto* stored_handler = new HandlerType(std::forward<Handler>(handler));
				auto Invoke = [](void* erased_handler, void* event) {
					std::invoke(
						*static_cast<HandlerType*>(erased_handler),
						*static_cast<Event*>(event)
					);
					};
				auto Destroy = [](void* erased_handler) noexcept {
					delete static_cast<HandlerType*>(erased_handler);
					};
				auto const subscription_id = m_next_subscription_id++;
				try {
					m_entries.emplace_back(
						node_id,
						subscription_id,
						Event::ID,
						routes,
						handled_events_too,
						stored_handler,
						Invoke,
						Destroy
					);
				}
				catch (...) {
					Destroy(stored_handler);
					throw;
				}
				return subscription_id;
			}

			template <RoutedEvent Event>
			void Dispatch(std::span<std::uint64_t const> route, Event& event) {
				auto InvokeNode = [this, &event](std::uint64_t node_id, RoutingStrategy phase) {
					std::vector<std::uint64_t> subscriptions;
					for (auto const& entry : m_entries) {
						if (entry.Matches(node_id, Event::ID, phase, event.handled)) {
							subscriptions.emplace_back(entry.GetSubscriptionID());
						}
					}
					for (auto const subscription_id : subscriptions) {
						auto matches_subscription = [subscription_id](Entry const& entry) {
							return entry.GetSubscriptionID() == subscription_id;
						};
						auto const iterator = std::ranges::find_if(
							m_entries,
							matches_subscription
						);
						if (iterator != m_entries.end() &&
							iterator->Matches(node_id, Event::ID, phase, event.handled)) {
							iterator->Invoke(&event);
						}
					}
					};
				if (route.empty()) {
					return;
				}
				if (static_cast<RoutingStrategy>(Event::Routing) & RoutingStrategy::Tunnel) {
					for (auto iterator = route.rbegin(); iterator != route.rend(); ++iterator) {
						InvokeNode(*iterator, RoutingStrategy::Tunnel);
					}
				}
				if (static_cast<RoutingStrategy>(Event::Routing) & RoutingStrategy::Direct) {
					InvokeNode(route.front(), RoutingStrategy::Direct);
				}
				if (static_cast<RoutingStrategy>(Event::Routing) & RoutingStrategy::Bubble) {
					for (auto const node_id : route) {
						InvokeNode(node_id, RoutingStrategy::Bubble);
					}
				}
			}

			void Unsubscribe(std::uint64_t node_id, std::uint64_t subscription_id) noexcept {
				std::erase_if(
					m_entries,
					[node_id, subscription_id](Entry const& entry) {
						return entry.GetNodeID() == node_id && entry.GetSubscriptionID() == subscription_id;
					}
				);
			}

			void Remove(std::uint64_t node_id) noexcept {
				std::erase_if(
					m_entries,
					[node_id](Entry const& entry) {
						return entry.GetNodeID() == node_id;
					}
				);
			}
		};

		struct NodeState {
			std::variant<Container, Widget> content;
			LayoutProperties layout;
			StyleOverride style;
			std::optional<std::uint64_t> parent;
			std::optional<std::uint64_t> child;
			std::optional<std::uint64_t> sibling;
		};

		std::vector<std::optional<NodeState>> m_nodes;
		EventStorage m_events;
		std::optional<std::uint64_t> m_window_layer_id;

		LogicalNode InsertNode(std::variant<Container, Widget> const& content);
		std::vector<std::uint64_t> BuildRoute(std::uint64_t source_id) const;

	public:
		explicit LogicalTree(Container const& root);
		~LogicalTree() noexcept = default;
		LogicalTree(LogicalTree const&) = delete;
		LogicalTree& operator=(LogicalTree const&) = delete;
		LogicalTree(LogicalTree&&) noexcept = default;
		LogicalTree& operator=(LogicalTree&&) noexcept = default;

		void AddChild(PassKey<LogicalNode>, LogicalNode const* parent, LogicalNode const& child);
		void SetLayout(PassKey<LogicalNode>, std::uint64_t id, LayoutProperties const& layout);
		void SetStyle(PassKey<LogicalNode>, std::uint64_t id, StyleOverride const& style);
		void BringToFront(PassKey<LogicalNode>, std::uint64_t id);
		LogicalNode Insert(PassKey<LogicalNode>, Container const& container);

		template <RoutedEvent Event, class Handler>
		std::uint64_t Subscribe(PassKey<LogicalNode>, std::uint64_t node_id, Handler&& handler, RoutingStrategy routes, bool handled_events_too) {
			return m_events.Subscribe<Event>(node_id, std::forward<Handler>(handler), routes, handled_events_too);
		}

		void Unsubscribe(PassKey<LogicalNode>, std::uint64_t node_id, std::uint64_t subscription_id) noexcept {
			m_events.Unsubscribe(node_id, subscription_id);
		}

		template <RoutedEvent Event>
		void Dispatch(PassKey<LogicalNode>, std::uint64_t source_id, Event& event) {
			auto const route = BuildRoute(source_id);
			m_events.Dispatch<Event>(route, event);
		}

		template <class WidgetType>
		WidgetType& GetWidget(PassKey<LogicalNode>, std::uint64_t node_id) {
			return std::get<WidgetType>(
				std::get<Widget>(m_nodes[node_id]->content)
			);
		}

		template <class ContainerType>
		ContainerType& GetContainer(PassKey<LogicalNode>, std::uint64_t node_id) {
			return std::get<ContainerType>(
				std::get<Container>(m_nodes[node_id]->content)
			);
		}

		LogicalNode GetRoot() noexcept;
		LogicalNode Insert(Widget widget);
		void Remove(std::uint64_t id) noexcept;
		LogicalNode GetNode(std::uint64_t id);

		/// Builds a value-owned visual snapshot for the requested viewport.
		/// Call chain: flatten the logical hierarchy -> measure from leaves to root
		/// -> arrange from root to leaves -> emit visual nodes in painting order.
		/// Every traversal is iterative; this function never follows the hierarchy
		/// through recursive calls.
		[[nodiscard]] VisualTree BuildVisualTree(Size const& available_size, Theme const& theme) const;
	};

	/// Identifies one node owned by a LogicalTree. The handle remains separate
	/// from Widget and Container; all node state remains owned by LogicalTree.
	class LogicalNode final {
	private:
		LogicalTree* m_tree;
		std::uint64_t m_id;

	public:
		LogicalNode(PassKey<LogicalTree>, LogicalTree* tree, std::uint64_t id) noexcept
			: m_tree(tree),
			m_id(id) {
		}

		LogicalNode(LogicalNode const&) noexcept = default;
		LogicalNode& operator=(LogicalNode const&) noexcept = default;
		LogicalNode(LogicalNode&&) noexcept = default;
		LogicalNode& operator=(LogicalNode&&) noexcept = default;

		LogicalNode AddChild(Widget widget);
		LogicalNode AddChild(Container container);

		/// Replaces this node's layout state. Call chain: LogicalNode -> guarded
		/// LogicalTree mutation -> BuildVisualTree measure and arrange passes.
		void SetLayout(LayoutProperties const& layout);

		/// Replaces this node's local style overrides. Foreground and font size
		/// propagate during BuildVisualTree; all other properties remain local.
		void SetStyle(StyleOverride const& style);

		/// Moves this node to the end of its parent's sibling chain. WindowLayer
		/// paints later siblings above earlier siblings, so this raises a window.
		void BringToFront();

		template <RoutedEvent Event, class Handler>
		std::uint64_t Subscribe(Handler&& handler, RoutingStrategy routes = Event::Routing, bool handled_events_too = false) {
			return m_tree->Subscribe<Event>(PassKey<LogicalNode>{},m_id, std::forward<Handler>(handler), routes, handled_events_too);
		}

		void Unsubscribe(std::uint64_t subscription_id) noexcept {
			m_tree->Unsubscribe(PassKey<LogicalNode>{}, m_id, subscription_id);
		}

		template <class WidgetType>
		WidgetType& GetWidget() {
			return m_tree->GetWidget<WidgetType>(PassKey<LogicalNode>{}, m_id);
		}

		template <class ContainerType>
		ContainerType& GetContainer() {
			return m_tree->GetContainer<ContainerType>(PassKey<LogicalNode>{}, m_id);
		}

		template <RoutedEvent Event>
		void Dispatch(Event& event) {
			m_tree->Dispatch(PassKey<LogicalNode>{}, m_id, event);
		}

		std::uint64_t GetID() const noexcept {
			return m_id;
		}
	};

}
