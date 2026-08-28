#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <vector>
#include <string>
#include <variant>
#include <string_view>
#endif

import fyuu_ui;
#if defined(__cpp_lib_modules)
import std;
#endif

int main() {
	fyuu_ui::LogicalTree tree{fyuu_ui::Overlay{}};
	auto events = tree.BuildEventBus();
	std::vector<fyuu_ui::SubscriptionHandle> subscriptions;
	auto root = tree.GetRoot();
	auto first = root.AddChild(fyuu_ui::TextBox{"first", {}, false, false, 0u});
	auto second = root.AddChild(fyuu_ui::TextBox{"second", {}, false, false, 0u});
	auto modal = root.AddChild(fyuu_ui::Overlay{});
	auto modal_button = modal.AddChild(fyuu_ui::Button{"OK"});
	auto focus_changes = 0u;
	subscriptions.emplace_back(events.Subscribe<fyuu_ui::FocusChangedEvent>(first, [&focus_changes](auto&) { ++focus_changes; }));
	if (!events.Focus(tree, first) || !first.AsWidget<fyuu_ui::TextBox>().focused)
		return 1;
	if (!events.MoveFocus(tree, fyuu_ui::FocusDirection::Next) || !events.IsFocused(second) ||
	    first.AsWidget<fyuu_ui::TextBox>().focused) {
		return 2;
	}

	// Removing the focused node advances to the next surviving focusable node.
	events.Remove(tree, second.GetID());
	if (!events.IsFocused(modal_button))
		return 3;

	if (!events.PushModalFocusScope(tree, modal) || events.Focus(tree, first) || !events.IsFocused(modal_button))
		return 4;
	if (!events.MoveFocus(tree, fyuu_ui::FocusDirection::Next) || !events.IsFocused(modal_button))
		return 5;
	if (!events.CapturePointer(tree, modal_button) || events.CapturedPointerNodeIDs().size() != 1u)
		return 6;
	auto pointer_events = 0u;
	subscriptions.emplace_back(events.Subscribe<fyuu_ui::PointerPressedEvent>(modal_button, [&pointer_events](auto& event) {
		++pointer_events;
		event.handled = true;
	}));
	fyuu_ui::PointerPressedEvent pointer{
	    {12.0f, 16.0f}, fyuu_ui::PointerButton::Left, 1u, false
	};
	events.Dispatch(tree, modal_button, pointer);
	if (!pointer.handled || pointer_events != 1u)
		return 7;
	auto value_events = 0u;
	subscriptions.emplace_back(events.Subscribe<fyuu_ui::ValueChangedEvent>(modal_button, [&value_events](auto& event) {
		if (std::get<float>(event.previous) == 1.0f &&
		    std::get<float>(event.current) == 2.0f) {
			++value_events;
		}
	}));
	fyuu_ui::ValueChangedEvent value{1.0f, 2.0f};
	events.Dispatch(tree, modal_button, value);
	if (value_events != 1u)
		return 8;
	auto self_unsubscribe_calls = 0u;
	fyuu_ui::SubscriptionHandle self_subscription;
	self_subscription = events.Subscribe<fyuu_ui::ClickEvent>(
		modal_button,
		[&](fyuu_ui::ClickEvent&) {
			++self_unsubscribe_calls;
			self_subscription.Reset();
		}
	);
	fyuu_ui::ClickEvent click{};
	events.Dispatch(tree, modal_button, click);
	events.Dispatch(tree, modal_button, click);
	if (self_unsubscribe_calls != 1u)
		return 8;

	// Removing the modal scope drops its boundary before selecting outside it.
	events.Remove(tree, modal.GetID());
	if (!events.IsFocused(first) || events.HasPointerCapture())
		return 9;
	if (events.FocusedNodeIDs().size() != 1u)
		return 10;
	if (focus_changes < 2u)
		return 11;

	// FocusManager contains no owner pointer, so moving the tree preserves IDs and
	// focus state without a rebind step or a separately allocated manager object.
	auto const first_id = first.GetID();
	fyuu_ui::LogicalTree moved_tree{std::move(tree)};
	if (!events.IsFocused(moved_tree.GetNode(first_id)))
		return 12;
	fyuu_ui::LogicalTree assigned_tree{fyuu_ui::Overlay{}};
	assigned_tree = std::move(moved_tree);
	if (!events.IsFocused(assigned_tree.GetNode(first_id)))
		return 13;

	// TextBox offsets are UTF-8 boundaries; selection-aware edits replace the
	// selected bytes and leave both ends of the selection at the new caret.
	fyuu_ui::TextBox editor{"alpha beta"};
	fyuu_ui::CollapseTextSelection(editor, 5u);
	fyuu_ui::MoveTextCaret(editor, 0u, true);
	if (fyuu_ui::CopySelectedText(editor) != "alpha")
		return 14;
	fyuu_ui::InsertText(editor, "A");
	if (editor.text != "A beta" || fyuu_ui::HasTextSelection(editor))
		return 15;
	fyuu_ui::SelectTextWord(editor, 3u);
	if (fyuu_ui::CopySelectedText(editor) != "beta")
		return 16;
	fyuu_ui::DeleteTextBackward(editor);
	if (editor.text != "A ")
		return 17;
	fyuu_ui::BeginTextEdit(editor);
	fyuu_ui::InsertText(editor, "changed");
	if (!fyuu_ui::CancelTextEdit(editor) || editor.text != "A ")
		return 18;
	fyuu_ui::InsertText(editor, "done");
	if (!fyuu_ui::CommitTextEdit(editor))
		return 19;
	auto const fixed_width = [](std::string_view text) { return static_cast<float>(text.size()); };
	fyuu_ui::CollapseTextSelection(editor, editor.text.size());
	fyuu_ui::EnsureTextCaretVisible(editor, 2.0f, fixed_width);
	if (editor.horizontal_offset <= 0.0f || fyuu_ui::TextOffsetAt(editor, 0.4f, fixed_width) != 0u)
		return 20;

	// Wheel routing starts at the hit child and finds its owning ScrollView. The
	// same embedded ScrollBar retains pressed state for the complete drag.
	fyuu_ui::LogicalTree scroll_tree{fyuu_ui::Overlay{}};
	auto scroll_root = scroll_tree.GetRoot();
	auto scroll_node = scroll_root.AddChild(fyuu_ui::ScrollView{});
	auto scroll_child = scroll_node.AddChild(fyuu_ui::Button{"Row"});
	auto& scroll_view = scroll_node.AsContainer<fyuu_ui::ScrollView>();
	scroll_view.viewport_extent = 100.0f;
	scroll_view.content_extent = 300.0f;
	auto const scroll_ancestors = scroll_tree.Ancestors(scroll_child.GetID());
	if (scroll_ancestors.size() < 2u || scroll_ancestors[1] != scroll_node.GetID())
		return 21;
	fyuu_ui::Scroll(scroll_view, -1.0f);
	if (scroll_view.offset != 36.0f)
		return 21;
	if (!fyuu_ui::BeginScrollBarDrag(
	        scroll_view, {96.0f, 20.0f}, {2.0f, 8.0f}, {8.0f, 32.0f}
	    ) || scroll_view.vertical_scroll_bar.interaction != fyuu_ui::InteractionState::Pressed) {
		return 22;
	}
	fyuu_ui::SetScrollBarInteraction(scroll_view, fyuu_ui::InteractionState::Hovered);
	if (scroll_view.vertical_scroll_bar.interaction != fyuu_ui::InteractionState::Pressed ||
	    !fyuu_ui::DragScrollBar(scroll_view, 70.0f) ||
	    !fyuu_ui::EndScrollBarDrag(scroll_view)) {
		return 23;
	}

	// DialogHost owns nested modality: only the top subtree is interactive, focus
	// cannot escape it, and opening a dialog cancels capture behind the modal layer.
	fyuu_ui::LogicalTree dialog_tree{fyuu_ui::Overlay{}};
	auto dialog_events = dialog_tree.BuildEventBus();
	auto dialog_root = dialog_tree.GetRoot();
	auto behind = dialog_root.AddChild(fyuu_ui::Button{"Behind"});
	dialog_root.AddChild(fyuu_ui::WindowLayer{});
	fyuu_ui::DialogHost dialogs{dialog_tree, dialog_events};
	dialog_events.CapturePointer(dialog_tree, behind);
	auto first_dialog = dialogs.Open(fyuu_ui::Window{"First"});
	auto first_button = first_dialog.AddChild(fyuu_ui::Button{"First action"});
	dialogs.Activate(first_dialog.GetID());
	if (dialog_events.HasPointerCapture() || dialogs.AllowsInput(behind.GetID()) ||
	    !dialogs.AllowsInput(first_button.GetID()) || dialog_events.Focus(dialog_tree, behind)) {
		return 24;
	}
	auto second_dialog = dialogs.Open(fyuu_ui::Window{"Second"});
	auto second_button = second_dialog.AddChild(fyuu_ui::Button{"Second action"});
	dialogs.Activate(second_dialog.GetID());
	if (dialogs.AllowsInput(first_button.GetID()) || !dialogs.AllowsInput(second_button.GetID()))
		return 25;
	dialogs.Close(second_dialog.GetID());
	if (!dialogs.AllowsInput(first_button.GetID()) || dialogs.AllowsInput(behind.GetID()))
		return 26;
	dialogs.Close(first_dialog.GetID());
	if (dialogs.IsOpen() || !dialogs.AllowsInput(behind.GetID()))
		return 27;

	// Moving EventBus rebinds every live SubscriptionHandle without allocating a shared
	// control block. Reset must therefore unsubscribe from the destination bus.
	fyuu_ui::LogicalTree movable_tree{fyuu_ui::Overlay{}};
	auto movable_button = movable_tree.GetRoot().AddChild(fyuu_ui::Button{"Move"});
	auto source_bus = movable_tree.BuildEventBus();
	auto moved_clicks = 0u;
	auto moved_handle = source_bus.Subscribe<fyuu_ui::ClickEvent>(
	    movable_button, [&moved_clicks](auto&) { ++moved_clicks; }
	);
	auto destination_bus = std::move(source_bus);
	fyuu_ui::ClickEvent moved_click{{}};
	destination_bus.Dispatch(movable_tree, movable_button, moved_click);
	moved_handle.Reset();
	destination_bus.Dispatch(movable_tree, movable_button, moved_click);
	if (moved_clicks != 1u)
		return 28;
	return 0;
}
