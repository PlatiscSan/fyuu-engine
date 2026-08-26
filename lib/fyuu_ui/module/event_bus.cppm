module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>
#include <vector>
#include <algorithm>
#include <functional>
#include <cstdint>
#include <type_traits>
#include <unordered_map>
#include <concepts>
#include <ranges>
#include <span>
#endif

export module fyuu_ui:event_bus;
#if defined(__cpp_lib_modules)
import std;
#endif
import :logical_tree;

export namespace fyuu_ui {
	enum class FocusDirection : std::uint8_t { Next, Previous };

	/// Owns routed-event handlers and all state needed while routing input. It keeps
	/// no LogicalTree pointer: every operation receives the current tree explicitly.
	/// Instances are created by LogicalTree::BuildEventBus so their node IDs share
	/// the same logical namespace as VisualTree hit-test results.
	class EventBus final {
	private:
		class Entry final {
		private:
			std::uint64_t m_node_id;
			std::uint64_t m_event_id;
			RoutingStrategy m_routes;
			bool m_handled_events_too;
			void* m_handler;
			void (*m_invoke)(void*, void*);
			void (*m_destroy)(void*) noexcept;
			bool m_active = true;

		public:
			/// Takes ownership of handler. invoke and destroy are the type-erased
			/// operations produced by Subscribe for the concrete handler type.
			Entry(
			    std::uint64_t node_id,
			    std::uint64_t event_id,
			    RoutingStrategy routes,
			    bool handled_events_too,
			    void* handler,
			    void (*invoke)(void*, void*),
			    void (*destroy)(void*) noexcept
			) noexcept :
			    m_node_id(node_id), m_event_id(event_id), m_routes(routes),
			    m_handled_events_too(handled_events_too), m_handler(handler), m_invoke(invoke),
			    m_destroy(destroy) {
			}

			~Entry() noexcept {
				if (m_destroy != nullptr) {
					m_destroy(m_handler);
				}
			}

			Entry(Entry const&) = delete;
			Entry& operator=(Entry const&) = delete;
			Entry(Entry&& other) noexcept :
			    m_node_id(other.m_node_id), m_event_id(other.m_event_id), m_routes(other.m_routes),
			    m_handled_events_too(other.m_handled_events_too),
			    m_handler(std::exchange(other.m_handler, nullptr)), m_invoke(other.m_invoke),
			    m_destroy(std::exchange(other.m_destroy, nullptr)), m_active(other.m_active) {
			}

			Entry& operator=(Entry&& other) noexcept {
				if (m_destroy != nullptr) {
					m_destroy(m_handler);
				}
				m_node_id = other.m_node_id;
				m_event_id = other.m_event_id;
				m_routes = other.m_routes;
				m_handled_events_too = other.m_handled_events_too;
				m_handler = std::exchange(other.m_handler, nullptr);
				m_invoke = other.m_invoke;
				m_destroy = std::exchange(other.m_destroy, nullptr);
				m_active = other.m_active;
				return *this;
			}

			std::uint64_t GetNodeID() const noexcept {
				return m_node_id;
			}

			bool Matches(
			    std::uint64_t node_id,
			    std::uint64_t event_id,
			    RoutingStrategy route,
			    bool handled
			) const noexcept {
				// A handled event continues only for explicitly opted-in handlers.
				return m_active && m_node_id == node_id && m_event_id == event_id &&
				    (m_routes & route) && (!handled || m_handled_events_too);
			}

			void Deactivate() noexcept {
				m_active = false;
			}
			[[nodiscard]] bool IsActive() const noexcept {
				return m_active;
			}

			void Invoke(void* event) {
				m_invoke(m_handler, event);
			}
		};

		using EventIndex = std::unordered_map<std::uint64_t, std::vector<std::uint64_t>>;
		// m_entries owns handlers; m_index contains non-owning subscription IDs.
		// Shape: node ID -> event ID -> subscriptions in registration order.
		std::unordered_map<std::uint64_t, Entry> m_entries;
		std::unordered_map<std::uint64_t, EventIndex> m_index;
		std::uint64_t m_next_subscription_id = 0u;
		std::size_t m_dispatch_depth = 0u;
		// Focus and modal scopes use stable logical-node IDs. Both vectors are owned
		// here because they are routing state, not part of the logical hierarchy.
		std::vector<std::uint64_t> m_focused;
		std::vector<std::uint64_t> m_modal_scopes;
		std::uint64_t m_pointer_capture_node_id = 0u;

		[[nodiscard]] bool IsFocusable(
		    LogicalTree const& tree,
		    std::uint64_t node_id
		) const noexcept;
		[[nodiscard]] std::vector<std::uint64_t> BuildFocusOrder(LogicalTree const& tree) const;
		[[nodiscard]] bool IsInsideActiveModalScope(
		    PassKey<EventBus>,
		    LogicalTree const& tree,
		    std::uint64_t node_id
		) const noexcept;
		void Clear(PassKey<EventBus>, LogicalTree& tree) noexcept;
		bool Focus(PassKey<EventBus>, LogicalTree& tree, std::uint64_t node_id);
		bool Move(PassKey<EventBus>, LogicalTree& tree, FocusDirection direction);
		bool PushModalScope(PassKey<EventBus>, LogicalTree& tree, std::uint64_t scope_id);
		void PopModalScope(PassKey<EventBus>, LogicalTree& tree, std::uint64_t scope_id) noexcept;
		void OnRemoving(
		    PassKey<EventBus>,
		    LogicalTree& tree,
		    std::span<std::uint64_t const> subtree
		) noexcept;

		void FinishDispatch() noexcept {
			--m_dispatch_depth;
			if (m_dispatch_depth == 0u) {
				std::erase_if(
					m_entries, 
					[](auto const& item) {
						return !item.second.IsActive();
					}
				);
			}
		}

		class DispatchScope final {
		private:
			EventBus* m_bus;

		public:
			explicit DispatchScope(EventBus* bus) noexcept : m_bus(bus) {
				++m_bus->m_dispatch_depth;
			}
			~DispatchScope() noexcept {
				m_bus->FinishDispatch();
			}
		};

		void PublishFocusChanges(
		    LogicalTree const& tree,
		    std::span<std::uint64_t const> before,
		    std::span<std::uint64_t const> after
		) {
			for (auto const node_id : before) {
				if (std::ranges::find(after, node_id) == after.end()) {
					try {
						FocusChangedEvent event{false};
						Dispatch(tree, node_id, event);
					} catch (...) {
					}
				}
			}
			for (auto const node_id : after) {
				if (std::ranges::find(before, node_id) == before.end()) {
					FocusChangedEvent event{true};
					Dispatch(tree, node_id, event);
				}
			}
		}

	public:
		explicit EventBus(PassKey<LogicalTree>) noexcept {
		}
		~EventBus() noexcept = default;
		EventBus(EventBus const&) = delete;
		EventBus& operator=(EventBus const&) = delete;
		EventBus(EventBus&&) noexcept = default;
		EventBus& operator=(EventBus&&) noexcept = default;

		bool Focus(LogicalTree& tree, LogicalNode const& node) {
			auto const before = m_focused;
			auto const changed = Focus(PassKey<EventBus>{}, tree, node.GetID());
			PublishFocusChanges(tree, before, m_focused);
			return changed;
		}

		bool MoveFocus(LogicalTree& tree, FocusDirection direction) {
			auto const before = m_focused;
			auto const changed = Move(PassKey<EventBus>{}, tree, direction);
			PublishFocusChanges(tree, before, m_focused);
			return changed;
		}

		void ClearFocus(LogicalTree& tree) {
			auto const before = m_focused;
			Clear(PassKey<EventBus>{}, tree);
			PublishFocusChanges(tree, before, m_focused);
		}

		void Remove(LogicalTree& tree, std::uint64_t node_id) noexcept {
			auto const before = m_focused;
			auto const subtree = tree.CollectSubtree(PassKey<EventBus>{}, node_id);
			OnRemoving(PassKey<EventBus>{}, tree, subtree);
			if (std::ranges::find(subtree, m_pointer_capture_node_id) != subtree.end())
				m_pointer_capture_node_id = 0u;
			tree.Remove(node_id);
			for (auto const removed_id : subtree)
				Remove(removed_id);
			try {
				PublishFocusChanges(tree, before, m_focused);
			} catch (...) {
			}
		}

		[[nodiscard]] bool IsFocused(LogicalNode const& node) const noexcept {
			return !m_focused.empty() && m_focused.front() == node.GetID();
		}
		[[nodiscard]] std::span<std::uint64_t const> FocusedNodeIDs() const noexcept {
			return m_focused;
		}
		bool PushModalFocusScope(LogicalTree& tree, LogicalNode const& scope) {
			return PushModalScope(PassKey<EventBus>{}, tree, scope.GetID());
		}
		void PopModalFocusScope(LogicalTree& tree, LogicalNode const& scope) noexcept {
			PopModalScope(PassKey<EventBus>{}, tree, scope.GetID());
		}
		[[nodiscard]] bool IsInsideActiveModalFocusScope(
		    LogicalTree const& tree,
		    LogicalNode const& node
		) const noexcept {
			return IsInsideActiveModalScope(PassKey<EventBus>{}, tree, node.GetID());
		}
		bool CapturePointer(LogicalTree const& tree, LogicalNode const& node) {
			if (!tree.IsInSubtree(0u, node.GetID()) || !IsInsideActiveModalFocusScope(tree, node))
				return false;
			m_pointer_capture_node_id = node.GetID();
			return true;
		}
		void ReleasePointer() noexcept {
			m_pointer_capture_node_id = 0u;
		}
		[[nodiscard]] bool HasPointerCapture() const noexcept {
			return m_pointer_capture_node_id != 0u;
		}
		[[nodiscard]] std::span<std::uint64_t const> CapturedPointerNodeIDs() const noexcept {
			return m_pointer_capture_node_id == 0u ? std::span<std::uint64_t const>{} :
			                                         std::span{&m_pointer_capture_node_id, 1u};
		}

		template <RoutedEvent Event, class Handler>
		std::uint64_t Subscribe(
		    std::uint64_t node_id,
		    Handler&& handler,
		    RoutingStrategy routes = Event::Routing,
		    bool handled_events_too = false
		) {
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
			// IDs are never reused, so an in-flight dispatch cannot accidentally
			// resolve a removed subscription to a newly registered handler.
			auto const subscription_id = m_next_subscription_id++;
			try {
				m_entries.emplace(
				    subscription_id,
				    Entry{
				        node_id,
				        Event::ID,
				        routes,
				        handled_events_too,
				        stored_handler,
				        Invoke,
				        Destroy
				    }
				);
				m_index[node_id][Event::ID].emplace_back(subscription_id);
			} catch (...) {
				m_entries.erase(subscription_id);
				throw;
			}
			return subscription_id;
		}

		template <RoutedEvent Event, class Handler>
		std::uint64_t Subscribe(
		    LogicalNode const& node,
		    Handler&& handler,
		    RoutingStrategy routes = Event::Routing,
		    bool handled_events_too = false
		) {
			return Subscribe<Event>(
			    node.GetID(),
			    std::forward<Handler>(handler),
			    routes,
			    handled_events_too
			);
		}

		template <RoutedEvent Event>
		void Dispatch(std::span<std::uint64_t const> route, Event& event) {
			if (route.empty())
				return;
			DispatchScope dispatch_scope{this};
			auto InvokeNode = [this, &event](std::uint64_t node_id, RoutingStrategy phase) {
				auto const node = m_index.find(node_id);
				if (node == m_index.end())
					return;
				auto const indexed = node->second.find(Event::ID);
				if (indexed == node->second.end())
					return;
				// Snapshot IDs because a callback may unsubscribe itself or another
				// handler. Each ID is resolved again immediately before invocation.
				auto const subscriptions = indexed->second;
				for (auto const subscription_id : subscriptions) {
					auto const iterator = m_entries.find(subscription_id);
					if (iterator != m_entries.end() &&
					    iterator->second.Matches(node_id, Event::ID, phase, event.handled)) {
						iterator->second.Invoke(&event);
					}
				}
			};
			// BuildRoute stores source first and root last. Tunnel therefore walks
			// backward, Direct visits only the source, and Bubble walks forward.
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

		template <RoutedEvent Event>
		void Dispatch(LogicalTree const& tree, std::uint64_t source_id, Event& event) {
			auto const route = tree.Ancestors(source_id);
			Dispatch<Event>(route, event);
		}

		template <RoutedEvent Event>
		void Dispatch(LogicalTree const& tree, LogicalNode const& source, Event& event) {
			Dispatch(tree, source.GetID(), event);
		}

		void Unsubscribe(std::uint64_t node_id, std::uint64_t subscription_id) noexcept {
			auto const entry = m_entries.find(subscription_id);
			if (entry == m_entries.end() || entry->second.GetNodeID() != node_id)
				return;
			if (auto const indexed = m_index.find(node_id); indexed != m_index.end()) {
				for (auto& [event_id, subscriptions] : indexed->second)
					std::erase(subscriptions, subscription_id);
			}
			if (m_dispatch_depth == 0u)
				m_entries.erase(entry);
			else
				entry->second.Deactivate();
		}

		void Remove(std::uint64_t node_id) noexcept {
			// Called while deleting a logical subtree; erase owning entries before
			// dropping the node's index so every captured handler is destroyed.
			auto indexed = m_index.find(node_id);
			if (indexed == m_index.end())
				return;
			for (auto const& [event_id, subscriptions] : indexed->second) {
				for (auto const subscription_id : subscriptions) {
					auto const entry = m_entries.find(subscription_id);
					if (entry == m_entries.end())
						continue;
					if (m_dispatch_depth == 0u)
						m_entries.erase(entry);
					else
						entry->second.Deactivate();
				}
			}
			m_index.erase(indexed);
		}
	};

	inline EventBus LogicalTree::BuildEventBus() const {
		return EventBus{PassKey<LogicalTree>{}};
	}
} // namespace fyuu_ui
