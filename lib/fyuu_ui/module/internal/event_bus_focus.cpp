module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <utility>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <span>
#endif

module fyuu_ui:event_bus_focus_impl;
#if defined(__cpp_lib_modules)
import std;
#endif
import :event_bus;

namespace fyuu_ui {
	void EventBus::Attach(PassKey<SubscriptionHandle>, SubscriptionHandle* handle) noexcept {
		handle->SetLinks(PassKey<EventBus>{}, nullptr, m_handles);
		if (m_handles != nullptr)
			m_handles->SetLinks(PassKey<EventBus>{}, handle, m_handles->Next(PassKey<EventBus>{}));
		m_handles = handle;
	}

	void EventBus::Detach(PassKey<SubscriptionHandle>, SubscriptionHandle* handle) noexcept {
		auto* previous = handle->Previous(PassKey<EventBus>{});
		auto* next = handle->Next(PassKey<EventBus>{});
		if (previous != nullptr)
			previous->SetLinks(PassKey<EventBus>{}, previous->Previous(PassKey<EventBus>{}), next);
		else
			m_handles = next;
		if (next != nullptr)
			next->SetLinks(PassKey<EventBus>{}, previous, next->Next(PassKey<EventBus>{}));
		handle->SetLinks(PassKey<EventBus>{}, nullptr, nullptr);
	}

	void EventBus::Replace(
	    PassKey<SubscriptionHandle>,
	    SubscriptionHandle* old_handle,
	    SubscriptionHandle* new_handle
	) noexcept {
		auto* previous = old_handle->Previous(PassKey<EventBus>{});
		auto* next = old_handle->Next(PassKey<EventBus>{});
		new_handle->SetLinks(PassKey<EventBus>{}, previous, next);
		if (previous != nullptr)
			previous->SetLinks(
			    PassKey<EventBus>{},
			    previous->Previous(PassKey<EventBus>{}),
			    new_handle
			);
		else
			m_handles = new_handle;
		if (next != nullptr)
			next->SetLinks(PassKey<EventBus>{}, new_handle, next->Next(PassKey<EventBus>{}));
		old_handle->SetLinks(PassKey<EventBus>{}, nullptr, nullptr);
	}

	EventBus::~EventBus() noexcept {
		for (auto* handle = m_handles; handle != nullptr;) {
			auto* next = handle->Next(PassKey<EventBus>{});
			handle->Rebind(PassKey<EventBus>{}, nullptr);
			handle->SetLinks(PassKey<EventBus>{}, nullptr, nullptr);
			handle = next;
		}
	}

	EventBus::EventBus(EventBus&& other) noexcept :
	    m_entries(std::move(other.m_entries)), m_index(std::move(other.m_index)),
	    m_next_subscription_id(other.m_next_subscription_id),
	    m_dispatch_depth(other.m_dispatch_depth), m_focused(std::move(other.m_focused)),
	    m_modal_scopes(std::move(other.m_modal_scopes)),
	    m_pointer_capture_node_id(other.m_pointer_capture_node_id),
	    m_handles(std::exchange(other.m_handles, nullptr)) {
		for (
		    auto* handle = m_handles; handle != nullptr; handle = handle->Next(PassKey<EventBus>{})
		)
			handle->Rebind(PassKey<EventBus>{}, this);
	}

	EventBus& EventBus::operator=(EventBus&& other) noexcept {
		if (this == &other)
			return *this;
		this->~EventBus();
		std::construct_at(this, std::move(other));
		return *this;
	}

	SubscriptionHandle::SubscriptionHandle(
	    PassKey<EventBus>,
	    EventBus* bus,
	    std::uint64_t subscription_id
	) noexcept : m_bus(bus), m_subscription_id(subscription_id) {
		m_bus->Attach(PassKey<SubscriptionHandle>{}, this);
	}

	SubscriptionHandle::~SubscriptionHandle() noexcept {
		Reset();
	}

	SubscriptionHandle& SubscriptionHandle::operator=(SubscriptionHandle&& other) noexcept {
		if (this == &other)
			return *this;
		Reset();
		m_bus = other.m_bus;
		m_subscription_id = other.m_subscription_id;
		if (m_bus != nullptr) {
			m_bus->Replace(PassKey<SubscriptionHandle>{}, &other, this);
			other.m_bus = nullptr;
		}
		return *this;
	}

	SubscriptionHandle::SubscriptionHandle(SubscriptionHandle&& other) noexcept :
	    m_bus(other.m_bus), m_subscription_id(other.m_subscription_id) {
		if (m_bus != nullptr) {
			m_bus->Replace(PassKey<SubscriptionHandle>{}, &other, this);
			other.m_bus = nullptr;
		}
	}

	void SubscriptionHandle::Reset() noexcept {
		if (m_bus == nullptr)
			return;
		auto* bus = std::exchange(m_bus, nullptr);
		bus->Detach(PassKey<SubscriptionHandle>{}, this);
		bus->Unsubscribe(m_subscription_id);
	}

	EventBus LogicalTree::BuildEventBus() const {
		return EventBus{PassKey<LogicalTree>{}};
	}

	bool EventBus::IsFocusable(LogicalTree const& tree, std::uint64_t node_id) const noexcept {
		// LogicalTree owns the policy that maps enabled/editable controls to a
		// focusable result; EventBus only owns routing state.
		return tree.IsFocusable(PassKey<EventBus>{}, node_id);
	}

	std::vector<std::uint64_t> EventBus::BuildFocusOrder(LogicalTree const& tree) const {
		// LogicalTree owns hierarchy traversal. Supplying the active scope as the root
		// naturally excludes every node behind a modal window from the Tab sequence.
		return tree.BuildFocusOrder(
		    PassKey<EventBus>{},
		    m_modal_scopes.empty() ? 0u : m_modal_scopes.back()
		);
	}

	bool EventBus::IsInsideActiveModalScope(
	    PassKey<EventBus>,
	    LogicalTree const& tree,
	    std::uint64_t node_id
	) const noexcept {
		if (m_modal_scopes.empty())
			return tree.IsAttached(PassKey<EventBus>{}, node_id);
		return tree.IsInSubtree(m_modal_scopes.back(), node_id);
	}

	void EventBus::Clear(PassKey<EventBus>, LogicalTree& tree) noexcept {
		if (m_focused.empty())
			return;
		// Update the control before erasing the ID; SetFocused safely tolerates a node
		// being absent during defensive cleanup paths.
		auto const id = m_focused.front();
		tree.SetFocused(PassKey<EventBus>{}, id, false);
		m_focused.clear();
	}

	bool EventBus::Focus(PassKey<EventBus> key, LogicalTree& tree, std::uint64_t node_id) {
		// Attachment is checked independently from focusability: detached nodes must
		// never enter focus state even if their control type would normally qualify.
		if (!tree.IsAttached(PassKey<EventBus>{}, node_id) ||
		    !IsInsideActiveModalScope(key, tree, node_id)) {
			return false;
		}
		if (!IsFocusable(tree, node_id))
			return false;
		// Re-focusing the same node is intentionally idempotent; resetting its caret or
		// focused visuals here would make repeated pointer presses observable.
		if (!m_focused.empty() && m_focused.front() == node_id)
			return true;
		Clear(key, tree);
		m_focused.emplace_back(node_id);
		tree.SetFocused(PassKey<EventBus>{}, node_id, true);
		return true;
	}

	bool EventBus::Move(PassKey<EventBus> key, LogicalTree& tree, FocusDirection direction) {
		auto const candidates = BuildFocusOrder(tree);
		if (candidates.empty()) {
			Clear(key, tree);
			return false;
		}
		// A missing current node behaves like focus entering the sequence: Next chooses
		// the first candidate and Previous chooses the last. Both directions wrap.
		auto current = candidates.end();
		if (!m_focused.empty())
			current = std::ranges::find(candidates, m_focused.front());
		std::uint64_t target;
		if (direction == FocusDirection::Next) {
			target = current == candidates.end() || ++current == candidates.end() ?
			    candidates.front() :
			    *current;
		} else {
			target = current == candidates.begin() || current == candidates.end() ?
			    candidates.back() :
			    *--current;
		}
		return Focus(key, tree, target);
	}

	bool EventBus::PushModalScope(
	    PassKey<EventBus> key,
	    LogicalTree& tree,
	    std::uint64_t scope_id
	) {
		if (!tree.IsAttached(PassKey<EventBus>{}, scope_id)) {
			return false;
		}
		m_modal_scopes.emplace_back(scope_id);
		// Opening a modal scope cannot leave focus behind it. Move selects the first
		// focusable descendant, or leaves focus empty when the scope has none.
		if (m_focused.empty() || !IsInsideActiveModalScope(key, tree, m_focused.front())) {
			Clear(key, tree);
			Move(key, tree, FocusDirection::Next);
		}
		return true;
	}

	void EventBus::PopModalScope(
	    PassKey<EventBus> key,
	    LogicalTree& tree,
	    std::uint64_t scope_id
	) noexcept {
		auto const found = std::ranges::find(m_modal_scopes, scope_id);
		if (found == m_modal_scopes.end())
			return;
		// Erasing through end also closes nested boundaries. Retaining a child modal
		// scope after its parent closes would create an unreachable focus island.
		m_modal_scopes.erase(found, m_modal_scopes.end());
		if (!m_focused.empty()) {
			if (!IsInsideActiveModalScope(key, tree, m_focused.front())) {
				Clear(key, tree);
				Move(key, tree, FocusDirection::Next);
			}
		}
	}

	void EventBus::OnRemoving(
	    PassKey<EventBus> key,
	    LogicalTree& tree,
	    std::span<std::uint64_t const> subtree
	) noexcept {
		// Modal boundaries inside the removed subtree cease to exist before selecting
		// a replacement; the replacement must obey the nearest surviving boundary.
		std::erase_if(m_modal_scopes, [subtree](std::uint64_t id) {
			return std::ranges::find(subtree, id) != subtree.end();
		});
		if (m_focused.empty() || std::ranges::find(subtree, m_focused.front()) == subtree.end()) {
			return;
		}
		// BuildFocusOrder must run while LogicalTree still has the old links. The old
		// focused position is the anchor for forward selection and wraparound.
		auto const removed_focus = m_focused.front();
		auto const order = BuildFocusOrder(tree);
		auto const current = std::ranges::find(order, removed_focus);
		Clear(key, tree);
		// Search after the deleted focus first, then wrap to the beginning. Every
		// candidate in the removed subtree is skipped before any slot is reset.
		auto FocusFirstSurvivor = [this, &key, &tree, subtree](auto begin, auto end) {
			for (auto iterator = begin; iterator != end; ++iterator) {
				if (std::ranges::find(subtree, *iterator) == subtree.end()) {
					return Focus(key, tree, *iterator);
				}
			}
			return false;
		};
		if (current != order.end() && FocusFirstSurvivor(current + 1, order.end()))
			return;
		FocusFirstSurvivor(order.begin(), current);
	}
} // namespace fyuu_ui
