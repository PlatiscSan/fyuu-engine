module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <vector>
#include <algorithm>
#include <string>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <filesystem>
#include <format>
#include <ranges>
#endif // !defined(__cpp_lib_modules)

module fyuu_studio:ui;

import fyuu_desktop;
import fyuu_ui;
import :ui_renderer;
import :recent_projects;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace fyuu_studio {
	namespace {
		std::string PathText(std::filesystem::path const& path) {
			auto const source = path.generic_u8string();
			std::string result;
			result.reserve(source.size());
			for (auto const code_unit : source) {
				result.push_back(static_cast<char>(code_unit));
			}
			return result;
		}
	} // namespace

	enum class StudioCommandType {
		NewProject,
		OpenProject,
		NewDocument,
		OpenDocument,
		SaveDocument
	};

	struct StudioCommand {
		StudioCommandType type;
		std::filesystem::path path;
		std::string project_name;
		std::filesystem::path asset_path;
	};

	class StudioUI final : public fyuu_desktop::EventSink {
	private:
		fyuu_ui::Theme m_theme;
		fyuu_ui::LogicalTree m_tree;
		fyuu_ui::EventBus m_events;
		fyuu_ui::DialogHost m_dialog_host;
		fyuu_ui::MessageBox m_error_box;
		RecentProjects m_recent_projects;
		std::optional<fyuu_ui::FileDialogue> m_file_dialog;
		std::vector<fyuu_ui::SubscriptionHandle> m_subscriptions;
		std::vector<StudioCommand> m_commands;
		std::string m_backend_name;
		std::string m_status_context = "Ready";
		std::filesystem::path m_project_path;
		std::filesystem::path m_asset_path;
		std::optional<std::uint64_t> m_pressed_node;
		std::optional<std::uint64_t> m_focused_node;
		std::optional<std::uint64_t> m_split_drag_node;
		std::optional<std::uint64_t> m_scroll_bar_node;
		std::optional<std::uint64_t> m_about_window_node;
		std::optional<fyuu_ui::SubscriptionHandle> m_about_subscription;
		std::optional<std::uint64_t> m_window_drag_node;
		std::optional<std::uint64_t> m_window_resize_node;
		std::optional<fyuu_ui::Rect> m_text_drag_bounds;
		fyuu_ui::HitTestRole m_pressed_hit_role = fyuu_ui::HitTestRole::Content;
		fyuu_ui::WindowResizeRegion m_window_resize_region = fyuu_ui::WindowResizeRegion::None;
		std::uint64_t m_main_split_node = 0u;
		std::uint64_t m_workspace_split_node = 0u;
		std::uint64_t m_scene_view_node = 0u;
		std::uint64_t m_status_node = 0u;
		std::uint64_t m_menu_bar_node = 0u;
		fyuu_ui::MenuPath m_pressed_menu_path;
		std::uint64_t m_window_layer_node = 0u;
		std::uint64_t m_page_node = 0u;
		std::uint64_t m_open_project_node = 0u;
		std::uint64_t m_new_project_node = 0u;
		std::vector<std::uint64_t> m_recent_project_nodes;
		std::uint64_t m_new_project_window = 0u;
		std::uint64_t m_project_name_node = 0u;
		std::uint64_t m_project_asset_node = 0u;
		std::uint64_t m_project_create_node = 0u;
		std::uint64_t m_project_cancel_node = 0u;
		std::filesystem::path m_new_project_directory;
		std::uint32_t m_logical_width;
		std::uint32_t m_logical_height;
		std::uint32_t m_pixel_width;
		std::uint32_t m_pixel_height;
		float m_pointer_x = 0.0f;
		float m_pointer_y = 0.0f;
		float m_split_drag_start = 0.0f;
		float m_split_drag_value = 0.0f;
		float m_split_drag_extent = 1.0f;
		float m_window_drag_offset_x = 0.0f;
		float m_window_drag_offset_y = 0.0f;
		fyuu_ui::Point m_window_resize_start;
		fyuu_ui::Point m_window_resize_position;
		fyuu_ui::Size m_window_resize_size;

		static fyuu_ui::LayoutProperties FixedHeight(float height, fyuu_ui::Alignment alignment) {
			fyuu_ui::LayoutProperties result;
			result.height = height;
			result.vertical_alignment = alignment;
			return result;
		}

		fyuu_ui::VisualTree BuildVisualTree() {
			auto const scale_x = m_logical_width == 0u ?
			    1.0f :
			    static_cast<float>(m_pixel_width) / static_cast<float>(m_logical_width);
			auto const scale_y = m_logical_height == 0u ?
			    1.0f :
			    static_cast<float>(m_pixel_height) / static_cast<float>(m_logical_height);
			return m_tree.BuildVisualTree(
			    {static_cast<float>(m_logical_width), static_cast<float>(m_logical_height)},
			    m_theme,
			    [scale_x, scale_y](std::string_view text, float font_size) {
				    return MeasureUIText(text, font_size, scale_x, scale_y);
			    }
			);
		}

		float MeasureTextWidth(std::string_view text) const {
			auto const scale_x = m_logical_width == 0u ?
			    1.0f :
			    static_cast<float>(m_pixel_width) / static_cast<float>(m_logical_width);
			auto const scale_y = m_logical_height == 0u ?
			    1.0f :
			    static_cast<float>(m_pixel_height) / static_cast<float>(m_logical_height);
			return MeasureUIText(text, m_theme.input.font_size, scale_x, scale_y).width;
		}

		void PositionTextCaret(std::uint64_t node_id, float x, bool extend, bool select_word) {
			auto& text_box = m_tree.GetNode(node_id).AsWidget<fyuu_ui::TextBox>();
			auto const measure = [this](std::string_view text) { return MeasureTextWidth(text); };
			auto const offset = fyuu_ui::TextOffsetAt(text_box, x + text_box.horizontal_offset, measure);
			if (select_word)
				fyuu_ui::SelectTextWord(text_box, offset);
			else
				fyuu_ui::MoveTextCaret(text_box, offset, extend);
			if (m_text_drag_bounds)
				fyuu_ui::EnsureTextCaretVisible(text_box, m_text_drag_bounds->size.width, measure);
		}

		static fyuu_ui::PointerButton ToPointerButton(fyuu_desktop::MouseButton button) noexcept {
			switch (button) {
				case fyuu_desktop::MouseButton::Unknown: return fyuu_ui::PointerButton::None;
				case fyuu_desktop::MouseButton::Left: return fyuu_ui::PointerButton::Left;
				case fyuu_desktop::MouseButton::Middle: return fyuu_ui::PointerButton::Middle;
				case fyuu_desktop::MouseButton::Right: return fyuu_ui::PointerButton::Right;
				case fyuu_desktop::MouseButton::Extra1: return fyuu_ui::PointerButton::Extra1;
				case fyuu_desktop::MouseButton::Extra2: return fyuu_ui::PointerButton::Extra2;
			}
			return fyuu_ui::PointerButton::None;
		}

		template <fyuu_ui::RoutedEvent Event>
		bool DispatchFocused(Event& event) {
			auto const focused = m_events.FocusedNodeIDs();
			if (focused.empty())
				return m_dialog_host.IsOpen();
			if (!m_dialog_host.AllowsInput(focused.front()))
				return true;
			m_events.Dispatch(m_tree, m_tree.GetNode(focused.front()), event);
			return event.handled;
		}

		template <fyuu_ui::RoutedEvent Event>
		bool DispatchPointer(Event& event, fyuu_ui::Point const& position) {
			auto const captured = m_events.CapturedPointerNodeIDs();
			if (!captured.empty()) {
				auto node = m_tree.GetNode(captured.front());
				if (!m_dialog_host.AllowsInput(node.GetID()))
					return true;
				m_events.Dispatch(m_tree, node, event);
				return event.handled;
			}
			auto const hit = BuildVisualTree().HitTest(position);
			if (!hit)
				return m_dialog_host.IsOpen();
			auto node = m_tree.GetNode(hit->logical_id);
			if (!m_dialog_host.AllowsInput(node.GetID()))
				return true;
			m_events.Dispatch(m_tree, node, event);
			return event.handled;
		}

		void UpdateStatus() {
			// The startup page has no status bar. Command failures can still be
			// reported there through MessageBox, so do not treat the root node ID
			// sentinel as a TextBlock while preparing that error dialog.
			if (m_status_node == 0u || !m_tree.IsInSubtree(0u, m_status_node)) {
				return;
			}
			auto& status = m_tree.GetNode(m_status_node).AsWidget<fyuu_ui::TextBlock>();
			status.text = std::format("{}    {}", m_status_context, m_backend_name);
		}

		void UpdateSplit(float x) {
			if (!m_split_drag_node) {
				return;
			}
			auto& split = m_tree.GetNode(*m_split_drag_node).AsContainer<fyuu_ui::SplitView>();
			split.split = std::clamp(
			    m_split_drag_value + (x - m_split_drag_start) / m_split_drag_extent,
			    0.0f,
			    1.0f
			);
		}

		bool Scroll(std::uint64_t node, float delta) {
			for (auto const ancestor : m_tree.Ancestors(node)) {
				if (auto* view =
				        m_tree.GetNode(ancestor).TryAsContainer<fyuu_ui::ScrollView>()) {
					fyuu_ui::Scroll(*view, delta);
					return true;
				}
			}
			return false;
		}

		void SetPointerState(std::uint64_t node, fyuu_ui::InteractionState state) {
			if (node == 0u) {
				return;
			}
			if (!m_tree.IsInSubtree(0u, node) || !m_dialog_host.AllowsInput(node)) {
				return;
			}
			if (m_file_dialog && m_file_dialog->SetInteraction(node, state)) {
				return;
			}
			if (m_error_box.SetInteraction(node, state)) {
				return;
			}
			if (node == m_main_split_node || node == m_workspace_split_node) {
				m_tree.GetNode(node).AsContainer<fyuu_ui::SplitView>().interaction = state;
				return;
			}
			if (auto* view = m_tree.GetNode(node).TryAsContainer<fyuu_ui::ScrollView>()) {
				fyuu_ui::SetScrollBarInteraction(*view, state);
				m_scroll_bar_node = node;
				return;
			}
			if (node == m_open_project_node || node == m_new_project_node ||
			    std::ranges::contains(m_recent_project_nodes, node)) {
				m_tree.GetNode(node).AsWidget<fyuu_ui::Button>().interaction = state;
			} else if (node == m_project_create_node || node == m_project_cancel_node) {
				m_tree.GetNode(node).AsWidget<fyuu_ui::Button>().interaction = state;
			}
		}

		void ClearPointerStates() {
			if (m_scroll_bar_node && m_tree.IsInSubtree(0u, *m_scroll_bar_node))
				if (auto* view = m_tree.GetNode(*m_scroll_bar_node)
				                     .TryAsContainer<fyuu_ui::ScrollView>())
					fyuu_ui::SetScrollBarInteraction(*view, fyuu_ui::InteractionState::Normal);
			m_scroll_bar_node.reset();
			if (m_file_dialog) {
				m_file_dialog->ClearInteractions();
			}
			m_error_box.ClearInteraction();
			if (m_about_window_node) {
				auto& window = m_tree.GetNode(*m_about_window_node).AsWidget<fyuu_ui::Window>();
				window.non_client_button_interaction = fyuu_ui::InteractionState::Normal;
			}
			SetPointerState(m_open_project_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_new_project_node, fyuu_ui::InteractionState::Normal);
			for (auto const node : m_recent_project_nodes)
				SetPointerState(node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_project_create_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_project_cancel_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_main_split_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_workspace_split_node, fyuu_ui::InteractionState::Normal);
		}

		void UpdatePointerState(float x, float y) {
			ClearPointerStates();
			if (m_pressed_node) {
				if (m_pressed_hit_role == fyuu_ui::HitTestRole::WindowNonClientButton) {
					auto& window = m_tree.GetNode(*m_pressed_node).AsWidget<fyuu_ui::Window>();
					window.non_client_button_interaction = fyuu_ui::InteractionState::Pressed;
				}
				SetPointerState(*m_pressed_node, fyuu_ui::InteractionState::Pressed);
				return;
			}
			auto tree = BuildVisualTree();
			auto const hit = tree.HitTest({x, y});
			if (hit) {
				SetPointerState(hit->logical_id, fyuu_ui::InteractionState::Hovered);
				if (m_menu_bar_node != 0u && hit->logical_id == m_menu_bar_node) {
					SetMenuHover(hit->menu_path);
				} else {
					SetMenuHover({});
				}
				if (hit->role == fyuu_ui::HitTestRole::WindowNonClientButton) {
					auto& window = m_tree.GetNode(hit->logical_id).AsWidget<fyuu_ui::Window>();
					window.non_client_button_interaction = fyuu_ui::InteractionState::Hovered;
				}
			} else {
				SetMenuHover({});
			}
		}

		bool IsTextBoxNode(std::uint64_t node_id) const noexcept {
			return node_id != 0u && (m_file_dialog->OwnsTextBox(node_id) ||
			    node_id == m_project_name_node || node_id == m_project_asset_node);
		}

		void TextBoxValueChanged(std::uint64_t node_id, std::string const& previous) {
			auto const& text = m_tree.GetNode(node_id).AsWidget<fyuu_ui::TextBox>().text;
			fyuu_ui::ValueChangedEvent changed{previous, text, false};
			m_events.Dispatch(m_tree, m_tree.GetNode(node_id), changed);
		}

		bool EditFocusedTextBox(fyuu_desktop::Event const& event) {
			if (!m_focused_node || !IsTextBoxNode(*m_focused_node))
				return false;
			auto const node_id = *m_focused_node;
			auto& text_box = m_tree.GetNode(node_id).AsWidget<fyuu_ui::TextBox>();
			if (event.type == fyuu_desktop::EventType::TextInput && !text_box.read_only) {
				auto const previous = text_box.text;
				fyuu_ui::InsertText(text_box, event.text);
				TextBoxValueChanged(node_id, previous);
				return true;
			}
			if (event.type != fyuu_desktop::EventType::KeyPressed)
				return false;

			auto ChangeText = [&](auto&& operation) {
				auto const previous = text_box.text;
				operation();
				if (text_box.text != previous)
					TextBoxValueChanged(node_id, previous);
			};
			if (event.control) {
				switch (event.key) {
				case fyuu_desktop::Key::A:
					fyuu_ui::SelectAllText(text_box);
					return true;
				case fyuu_desktop::Key::C:
					if (fyuu_ui::HasTextSelection(text_box))
						fyuu_desktop::ClipboardText(fyuu_ui::CopySelectedText(text_box));
					return true;
				case fyuu_desktop::Key::X:
					if (!text_box.read_only && fyuu_ui::HasTextSelection(text_box)) {
						auto const selected = fyuu_ui::CopySelectedText(text_box);
						ChangeText([&] { fyuu_ui::EraseTextSelection(text_box); });
						fyuu_desktop::ClipboardText(selected);
					}
					return true;
				case fyuu_desktop::Key::V:
					if (!text_box.read_only)
						ChangeText([&] { fyuu_ui::InsertText(text_box, fyuu_desktop::ClipboardText()); });
					return true;
				default: break;
				}
			}
			switch (event.key) {
			case fyuu_desktop::Key::LeftArrow:
				fyuu_ui::MoveTextCaret(
				    text_box, fyuu_ui::PreviousTextOffset(text_box.text, text_box.caret_offset), event.shift
				);
				return true;
			case fyuu_desktop::Key::RightArrow:
				fyuu_ui::MoveTextCaret(
				    text_box, fyuu_ui::NextTextOffset(text_box.text, text_box.caret_offset), event.shift
				);
				return true;
			case fyuu_desktop::Key::Home:
				fyuu_ui::MoveTextCaret(text_box, 0u, event.shift);
				return true;
			case fyuu_desktop::Key::End:
				fyuu_ui::MoveTextCaret(text_box, text_box.text.size(), event.shift);
				return true;
			case fyuu_desktop::Key::Backspace:
				if (!text_box.read_only)
					ChangeText([&] { fyuu_ui::DeleteTextBackward(text_box); });
				return true;
			case fyuu_desktop::Key::Delete:
				if (!text_box.read_only)
					ChangeText([&] { fyuu_ui::DeleteTextForward(text_box); });
				return true;
			case fyuu_desktop::Key::Enter: {
				(void)fyuu_ui::CommitTextEdit(text_box);
				fyuu_ui::TextSubmittedEvent submitted{text_box.text, false};
				m_events.Dispatch(m_tree, m_tree.GetNode(node_id), submitted);
				if (m_file_dialog->OwnsTextBox(node_id))
					m_file_dialog->CommitTextBox(node_id);
				return true;
			}
			case fyuu_desktop::Key::Escape: {
				auto const previous = text_box.text;
				if (fyuu_ui::CancelTextEdit(text_box))
					TextBoxValueChanged(node_id, previous);
				fyuu_ui::TextCancelledEvent cancelled{text_box.text, false};
				m_events.Dispatch(m_tree, m_tree.GetNode(node_id), cancelled);
				return true;
			}
			default: return false;
			}
		}

		void CloseMenu() {
			if (m_menu_bar_node == 0u) {
				return;
			}
			auto& bar = m_tree.GetNode(m_menu_bar_node).AsWidget<fyuu_ui::MenuBar>();
			bar.open_path.clear();
			bar.hover_path.clear();
			bar.pressed_path.clear();
		}

		void SetMenuHover(fyuu_ui::MenuPath const& path) {
			if (m_menu_bar_node == 0u) {
				return;
			}
			auto& bar = m_tree.GetNode(m_menu_bar_node).AsWidget<fyuu_ui::MenuBar>();
			if (path.indices.empty()) {
				bar.hover_path.clear();
				if (bar.open_path.size() > 1u) {
					bar.open_path.erase(bar.open_path.begin() + 1, bar.open_path.end());
				}
				return;
			}
			auto const& p = path.indices;
			bar.hover_path = p;
			if (p.size() == 1u && !bar.open_path.empty()) {
				if (bar.open_path[0u] == p[0u]) {
					bar.open_path.resize(1u);
				} else if (!bar.entries[p[0u]].children.empty()) {
					bar.open_path = p;
					m_status_context = std::format("{} menu", bar.entries[p[0u]].title);
					UpdateStatus();
				}
				return;
			}
			if (p.size() >= 2u && !bar.open_path.empty()) {
				auto const* entry = fyuu_ui::FindMenuEntry(bar, p);
				if (entry != nullptr && !entry->children.empty() && bar.open_path != p) {
					bar.open_path = p;
				} else if (
				    entry != nullptr && entry->children.empty() && bar.open_path.size() >= p.size()
				) {
					bar.open_path.assign(p.begin(), p.end() - 1);
				}
			}
		}

		void ShowAboutWindow() {
			// Help -> About calls this function through ActivateMenuPath. The first
			// activation inserts the window and its retained content into WindowLayer;
			// later activations reuse the same logical subtree and only move it above
			// its siblings. BuildVisualTree observes that new sibling order next frame.
			if (m_about_window_node) {
				auto window = m_tree.GetNode(*m_about_window_node);
				window.BringToFront();
				return;
			}
			auto layer = m_tree.GetNode(m_window_layer_node);
			auto window = layer.AddChild(
			    fyuu_ui::Window{"About Fyuu Studio", {420.0f, 180.0f}, {360.0f, 180.0f}, true}
			);
			m_about_window_node = window.GetID();
			m_about_subscription = m_events.Subscribe<fyuu_ui::ClickEvent>(window, [this](fyuu_ui::ClickEvent& event) {
				auto about = m_tree.GetNode(*m_about_window_node);
				about.BringToFront();
				event.handled = true;
			});
			auto content =
			    window.AddChild(fyuu_ui::StackPanel{fyuu_ui::Orientation::Vertical, 8.0f});
			fyuu_ui::LayoutProperties content_layout;
			content_layout.margin = {16.0f, 12.0f, 16.0f, 12.0f};
			content.SetLayout(content_layout);
			content.AddChild(fyuu_ui::TextBlock{"Fyuu Studio", m_theme.window_client_text, 16.0f});
			content.AddChild(
			    fyuu_ui::TextBlock{"Built with FyuuUI", m_theme.window_client_muted_text, 14.0f}
			);
		}

		void UpdateWindowPosition(float x, float y) {
			if (!m_window_drag_node) {
				return;
			}
			auto& window = m_tree.GetNode(*m_window_drag_node).AsWidget<fyuu_ui::Window>();
			window.position.x = std::max(0.0f, x - m_window_drag_offset_x);
			window.position.y = std::max(0.0f, y - m_window_drag_offset_y);
		}

		void UpdateWindowSize(float x, float y) {
			if (!m_window_resize_node) {
				return;
			}
			auto& window = m_tree.GetNode(*m_window_resize_node).AsWidget<fyuu_ui::Window>();
			auto const delta_x = x - m_window_resize_start.x;
			auto const delta_y = y - m_window_resize_start.y;
			auto const resize_left = m_window_resize_region == fyuu_ui::WindowResizeRegion::Left ||
			    m_window_resize_region == fyuu_ui::WindowResizeRegion::TopLeft ||
			    m_window_resize_region == fyuu_ui::WindowResizeRegion::BottomLeft;
			auto const resize_top = m_window_resize_region == fyuu_ui::WindowResizeRegion::Top ||
			    m_window_resize_region == fyuu_ui::WindowResizeRegion::TopLeft ||
			    m_window_resize_region == fyuu_ui::WindowResizeRegion::TopRight;
			auto const resize_right =
			    m_window_resize_region == fyuu_ui::WindowResizeRegion::Right ||
			    m_window_resize_region == fyuu_ui::WindowResizeRegion::TopRight ||
			    m_window_resize_region == fyuu_ui::WindowResizeRegion::BottomRight;
			auto const resize_bottom =
			    m_window_resize_region == fyuu_ui::WindowResizeRegion::Bottom ||
			    m_window_resize_region == fyuu_ui::WindowResizeRegion::BottomLeft ||
			    m_window_resize_region == fyuu_ui::WindowResizeRegion::BottomRight;
			if (resize_left) {
				auto const maximum_x = m_window_resize_position.x + m_window_resize_size.width -
				    window.minimum_size.width;
				window.position.x =
				    std::clamp(m_window_resize_position.x + delta_x, 0.0f, maximum_x);
				window.size.width =
				    m_window_resize_size.width + m_window_resize_position.x - window.position.x;
			} else if (resize_right) {
				window.size.width = std::clamp(
				    m_window_resize_size.width + delta_x,
				    window.minimum_size.width,
				    static_cast<float>(m_logical_width) - window.position.x
				);
			}
			if (resize_top) {
				auto const maximum_y = m_window_resize_position.y + m_window_resize_size.height -
				    window.minimum_size.height;
				window.position.y =
				    std::clamp(m_window_resize_position.y + delta_y, 0.0f, maximum_y);
				window.size.height =
				    m_window_resize_size.height + m_window_resize_position.y - window.position.y;
			} else if (resize_bottom) {
				window.size.height = std::clamp(
				    m_window_resize_size.height + delta_y,
				    window.minimum_size.height,
				    static_cast<float>(m_logical_height) - window.position.y
				);
			}
		}

		void FocusWindow(float x, float y) {
			if (!m_about_window_node) {
				return;
			}
			auto window = m_tree.GetNode(*m_about_window_node);
			auto& state = window.AsWidget<fyuu_ui::Window>();
			auto const inside = x >= state.position.x && y >= state.position.y &&
			    x < state.position.x + state.size.width && y < state.position.y + state.size.height;
			if (inside) {
				window.BringToFront();
			} else {
				state.active = false;
			}
		}

		void CloseAboutWindow() {
			if (!m_about_window_node) {
				return;
			}
			m_about_subscription.reset();
			m_events.Remove(m_tree, *m_about_window_node);
			m_about_window_node.reset();
			m_window_drag_node.reset();
			m_window_resize_node.reset();
		}

		bool BeginWindowInteraction(fyuu_ui::HitTestResult const& hit) {
			// These roles are emitted only by Window. Handling them by role keeps
			// dragging and resizing independent of which helper created the window.
			if (hit.role != fyuu_ui::HitTestRole::WindowNonClient &&
			    hit.role != fyuu_ui::HitTestRole::WindowNonClientButton &&
			    hit.role != fyuu_ui::HitTestRole::WindowResize) {
				return false;
			}
			auto window = m_tree.GetNode(hit.logical_id);
			window.BringToFront();
			auto const& state = window.AsWidget<fyuu_ui::Window>();
			m_pressed_node = hit.logical_id;
			m_pressed_hit_role = hit.role;
			if (state.closable && hit.role == fyuu_ui::HitTestRole::WindowNonClientButton) {
				if (m_file_dialog->OwnsWindow(hit.logical_id)) {
					if (m_focused_node && m_file_dialog->OwnsTextBox(*m_focused_node)) {
						m_focused_node.reset();
					}
					m_file_dialog->Cancel();
				} else if (m_error_box.OwnsWindow(hit.logical_id)) {
					m_error_box.Close();
				} else if (m_new_project_window != 0u &&
				    hit.logical_id == m_new_project_window) {
					m_dialog_host.Close(m_new_project_window);
					m_new_project_window = 0u;
					m_project_name_node = 0u;
					m_project_asset_node = 0u;
					m_project_create_node = 0u;
					m_project_cancel_node = 0u;
				} else if (m_about_window_node && hit.logical_id == *m_about_window_node) {
					CloseAboutWindow();
				}
				m_pressed_node.reset();
				m_pressed_hit_role = fyuu_ui::HitTestRole::Content;
				return true;
			}
			if (state.resizable && hit.role == fyuu_ui::HitTestRole::WindowResize) {
				m_window_resize_node = hit.logical_id;
				m_window_resize_region = hit.resize_region;
				m_window_resize_start = {m_pointer_x, m_pointer_y};
				m_window_resize_position = state.position;
				m_window_resize_size = state.size;
				return true;
			}
			m_window_drag_node = hit.logical_id;
			m_window_drag_offset_x = hit.position.x;
			m_window_drag_offset_y = hit.position.y;
			return true;
		}

		void ShowOpenFileDialog() {
			fyuu_ui::FileDialogOptions options;
			options.title = "Open scene";
			options.initial_directory = m_asset_path / "Scene";
			options.filters.emplace_back(
			    fyuu_ui::FileDialogFilter{"Fyuu scenes", {"fyuu", "json"}}
			);
			m_file_dialog->ShowOpen(options, [this](std::filesystem::path const& path) {
				m_status_context =
				    path.empty() ? "Open cancelled" : std::format("Open: {}", PathText(path));
				if (!path.empty()) {
					m_commands.emplace_back(StudioCommand{
						StudioCommandType::OpenDocument,
						path,
						{},
						{}
					});
				}
				UpdateStatus();
			});
		}

		void ShowOpenProjectDialog() {
			fyuu_ui::FileDialogOptions options;
			options.title = "Open project";
			options.filters.emplace_back(
				fyuu_ui::FileDialogFilter{"Fyuu projects", {"fyuu-project"}}
			);
			m_file_dialog->ShowOpen(options, [this](std::filesystem::path const& path) {
				if (!path.empty()) {
					m_commands.emplace_back(StudioCommand{
						StudioCommandType::OpenProject,
						path,
						{},
						{}
					});
				}
			});
		}

		void ShowNewProjectDialog() {
			fyuu_ui::FileDialogOptions options;
			options.title = "Select project location";
			m_file_dialog->ShowSelectDirectory(options, [this](std::filesystem::path const& directory) {
				if (directory.empty())
					return;
				m_new_project_directory = directory;
				auto window = m_dialog_host.Open(fyuu_ui::Window{
					"Project settings", {300.0f, 180.0f}, {520.0f, 260.0f}, true
				});
				m_new_project_window = window.GetID();
				auto content = window.AddChild(
					fyuu_ui::StackPanel{fyuu_ui::Orientation::Vertical, 12.0f}
				);
				fyuu_ui::LayoutProperties content_layout;
				content_layout.margin = {20.0f, 18.0f, 20.0f, 18.0f};
				content.SetLayout(content_layout);
				content.AddChild(fyuu_ui::TextBlock{
					std::format("Location: {}", PathText(directory)), m_theme.muted_text
				});
				auto name_row = content.AddChild(fyuu_ui::Overlay{});
				name_row.SetLayout(FixedHeight(32.0f, fyuu_ui::Alignment::Start));
				auto name_label = name_row.AddChild(fyuu_ui::TextBlock{"Project name", m_theme.text});
				fyuu_ui::LayoutProperties label_layout;
				label_layout.width = 104.0f;
				label_layout.vertical_alignment = fyuu_ui::Alignment::Center;
				name_label.SetLayout(label_layout);
				auto name = name_row.AddChild(fyuu_ui::TextBox{"NewProject", "Project name"});
				fyuu_ui::LayoutProperties input_layout;
				input_layout.margin.left = 112.0f;
				name.SetLayout(input_layout);
				m_project_name_node = name.GetID();
				auto asset_row = content.AddChild(fyuu_ui::Overlay{});
				asset_row.SetLayout(FixedHeight(32.0f, fyuu_ui::Alignment::Start));
				auto asset_label = asset_row.AddChild(fyuu_ui::TextBlock{"Asset directory", m_theme.text});
				asset_label.SetLayout(label_layout);
				auto asset = asset_row.AddChild(fyuu_ui::TextBox{"asset", "Relative directory"});
				asset.SetLayout(input_layout);
				m_project_asset_node = asset.GetID();
				auto actions = content.AddChild(
					fyuu_ui::StackPanel{fyuu_ui::Orientation::Horizontal, 8.0f}
				);
				fyuu_ui::LayoutProperties actions_layout;
				actions_layout.horizontal_alignment = fyuu_ui::Alignment::End;
				actions.SetLayout(actions_layout);
				auto create = actions.AddChild(fyuu_ui::Button{"Create project", true, true});
				auto cancel = actions.AddChild(fyuu_ui::Button{"Cancel", true, false});
				m_project_create_node = create.GetID();
				m_project_cancel_node = cancel.GetID();
				m_subscriptions.emplace_back(m_events.Subscribe<fyuu_ui::ClickEvent>(create,
					[this](fyuu_ui::ClickEvent& event) {
						auto const& project_name = m_tree.GetNode(m_project_name_node).AsWidget<fyuu_ui::TextBox>().text;
						auto const& asset_text = m_tree.GetNode(m_project_asset_node).AsWidget<fyuu_ui::TextBox>().text;
						auto const project_name_utf8 = project_name |
						    std::views::transform([](char code_unit) {
							    return static_cast<char8_t>(code_unit);
						    }) |
						    std::ranges::to<std::u8string>();
						std::filesystem::path const project_name_path{project_name_utf8};
						auto const asset_path = std::filesystem::path{
						    asset_text |
						    std::views::transform([](char code_unit) {
							    return static_cast<char8_t>(code_unit);
						    }) |
						    std::ranges::to<std::u8string>()
						}.lexically_normal();
						if (project_name.empty() || project_name_path.filename() != project_name_path ||
						    project_name_path.has_parent_path() || asset_path.empty() ||
						    asset_path.is_absolute() || *asset_path.begin() == "..") {
							m_error_box.Show("Invalid project", "Enter a project name and a relative asset directory.");
							event.handled = true;
							return;
						}
						auto project_path = m_new_project_directory / project_name_path /
						    project_name_path;
						project_path += ".fyuu-project";
						m_commands.emplace_back(StudioCommand{
							StudioCommandType::NewProject, project_path, project_name, asset_path
						});
						m_dialog_host.Close(m_new_project_window);
						m_new_project_window = 0u;
						m_project_name_node = 0u;
						m_project_asset_node = 0u;
						m_project_create_node = 0u;
						m_project_cancel_node = 0u;
						event.handled = true;
					}
				));
				m_subscriptions.emplace_back(m_events.Subscribe<fyuu_ui::ClickEvent>(cancel,
					[this](fyuu_ui::ClickEvent& event) {
						m_dialog_host.Close(m_new_project_window);
						m_new_project_window = 0u;
						m_project_name_node = 0u;
						m_project_asset_node = 0u;
						m_project_create_node = 0u;
						m_project_cancel_node = 0u;
						event.handled = true;
					}
				));
			});
		}

		void ShowSaveFileDialog() {
			fyuu_ui::FileDialogOptions options;
			options.title = "Save scene as";
			options.initial_file_name = "Scene";
			options.filters.emplace_back(fyuu_ui::FileDialogFilter{"Fyuu scenes", {"fyuu"}});
			m_file_dialog->ShowSave(options, [this](std::filesystem::path const& path) {
				m_status_context =
				    path.empty() ? "Save As cancelled" : std::format("Save As: {}", PathText(path));
				UpdateStatus();
			});
		}

		void ActivateMenuPath(fyuu_ui::MenuPath const& path) {
			auto& bar = m_tree.GetNode(m_menu_bar_node).AsWidget<fyuu_ui::MenuBar>();
			auto const* entry = fyuu_ui::FindMenuEntry(bar, path.indices);
			if (entry == nullptr || !entry->enabled) {
				return;
			}
			m_status_context = entry->title;
			if (path.indices == std::vector<std::uint32_t>{0u, 0u, 0u}) {
				m_commands.emplace_back(StudioCommand{
					StudioCommandType::NewDocument,
					{},
					{},
					{}
				});
			} else if (path.indices == std::vector<std::uint32_t>{0u, 1u}) {
				ShowOpenFileDialog();
			} else if (path.indices == std::vector<std::uint32_t>{0u, 2u}) {
				m_commands.emplace_back(StudioCommand{
					StudioCommandType::SaveDocument,
					{},
					{},
					{}
				});
			} else if (path.indices == std::vector<std::uint32_t>{0u, 3u}) {
				ShowSaveFileDialog();
			} else if (path.indices == std::vector<std::uint32_t>{2u, 3u}) {
				auto& main = m_tree.GetNode(m_main_split_node).AsContainer<fyuu_ui::SplitView>();
				auto& workspace =
				    m_tree.GetNode(m_workspace_split_node).AsContainer<fyuu_ui::SplitView>();
				main.split = 0.19f;
				workspace.split = 0.74f;
			} else if (path.indices == std::vector<std::uint32_t>{4u, 3u}) {
				ShowAboutWindow();
			}
			CloseMenu();
			UpdateStatus();
		}

		void BuildStartupPage() {
			auto root = m_tree.GetRoot();
			auto page = root.AddChild(fyuu_ui::Overlay{true});
			m_page_node = page.GetID();
			page.AddChild(fyuu_ui::Border{m_theme.background});
			auto title = page.AddChild(
				fyuu_ui::TextBlock{"Fyuu Studio", m_theme.text, m_theme.display_font_size}
			);
			fyuu_ui::LayoutProperties title_layout;
			title_layout.margin = {24.0f, 22.0f, 24.0f, 0.0f};
			title_layout.vertical_alignment = fyuu_ui::Alignment::Start;
			title.SetLayout(title_layout);
			auto heading = page.AddChild(
				fyuu_ui::TextBlock{"Get started", m_theme.text, m_theme.heading_font_size}
			);
			fyuu_ui::LayoutProperties heading_layout;
			heading_layout.margin = {24.0f, 76.0f, 24.0f, 0.0f};
			heading_layout.vertical_alignment = fyuu_ui::Alignment::Start;
			heading.SetLayout(heading_layout);

			auto body = page.AddChild(fyuu_ui::SplitView{
				fyuu_ui::Orientation::Horizontal, 0.74f, 420.0f, 240.0f, 24.0f, false
			});
			fyuu_ui::LayoutProperties body_layout;
			body_layout.margin = {24.0f, 112.0f, 24.0f, 28.0f};
			body.SetLayout(body_layout);
			auto recent = body.AddChild(fyuu_ui::Overlay{true});
			recent.AddChild(fyuu_ui::Border{m_theme.surface});
			auto recent_header = recent.AddChild(fyuu_ui::Border{m_theme.raised_surface});
			recent_header.SetLayout(FixedHeight(42.0f, fyuu_ui::Alignment::Start));
			auto recent_title = recent_header.AddChild(
				fyuu_ui::TextBlock{"Recent projects", m_theme.text}
			);
			fyuu_ui::LayoutProperties recent_title_layout;
			recent_title_layout.margin.left = 14.0f;
			recent_title_layout.vertical_alignment = fyuu_ui::Alignment::Center;
			recent_title.SetLayout(recent_title_layout);
			m_recent_project_nodes.clear();
			if (m_recent_projects.Projects().empty()) {
				auto empty_recent = recent.AddChild(
					fyuu_ui::TextBlock{"No recent projects", m_theme.muted_text}
				);
				fyuu_ui::LayoutProperties empty_recent_layout;
				empty_recent_layout.horizontal_alignment = fyuu_ui::Alignment::Center;
				empty_recent_layout.vertical_alignment = fyuu_ui::Alignment::Center;
				empty_recent.SetLayout(empty_recent_layout);
			} else {
				auto recent_view = recent.AddChild(fyuu_ui::ScrollView{});
				fyuu_ui::LayoutProperties recent_view_layout;
				recent_view_layout.margin = {10.0f, 52.0f, 10.0f, 10.0f};
				recent_view.SetLayout(recent_view_layout);
				auto recent_list = recent_view.AddChild(
					fyuu_ui::StackPanel{fyuu_ui::Orientation::Vertical, 6.0f}
				);
				for (auto const& project : m_recent_projects.Projects()) {
					auto item = recent_list.AddChild(fyuu_ui::Button{
						std::format("{}    {}", PathText(project.stem()), PathText(project.parent_path())),
						true,
						false
					});
					item.SetLayout(FixedHeight(44.0f, fyuu_ui::Alignment::Start));
					m_recent_project_nodes.emplace_back(item.GetID());
					m_subscriptions.emplace_back(m_events.Subscribe<fyuu_ui::ClickEvent>(
						item,
						[this, project](fyuu_ui::ClickEvent& event) {
							auto const already_queued = std::ranges::any_of(
								m_commands,
								[&project](StudioCommand const& command) {
									return command.type == StudioCommandType::OpenProject &&
									    command.path == project;
								}
							);
							if (!already_queued) {
								m_commands.emplace_back(StudioCommand{
									StudioCommandType::OpenProject, project, {}, {}
								});
							}
							event.handled = true;
						}
					));
				}
			}

			auto actions = body.AddChild(
				fyuu_ui::StackPanel{fyuu_ui::Orientation::Vertical, 10.0f}
			);
			actions.AddChild(fyuu_ui::TextBlock{"Start", m_theme.muted_text});
			auto create = actions.AddChild(
				fyuu_ui::Button{"Create a new project", true, true}
			);
			auto open = actions.AddChild(
				fyuu_ui::Button{"Open a project", true, false}
			);
			m_open_project_node = open.GetID();
			m_new_project_node = create.GetID();
			fyuu_ui::LayoutProperties button_layout;
			button_layout.height = 44.0f;
			open.SetLayout(button_layout);
			create.SetLayout(button_layout);
			m_subscriptions.emplace_back(m_events.Subscribe<fyuu_ui::ClickEvent>(open,
				[this](fyuu_ui::ClickEvent& event) {
					ShowOpenProjectDialog();
					event.handled = true;
				}
			));
			m_subscriptions.emplace_back(m_events.Subscribe<fyuu_ui::ClickEvent>(create,
				[this](fyuu_ui::ClickEvent& event) {
					ShowNewProjectDialog();
					event.handled = true;
				}
			));
			auto window_layer = root.AddChild(fyuu_ui::WindowLayer{});
			m_window_layer_node = window_layer.GetID();
		}

		void BuildEditorShell(std::string_view backend_name) {
			auto root = m_tree.GetRoot();
			std::vector<fyuu_ui::MenuEntry> menu_entries{
			    {"File",
			        true,
			        false,
			        {{"New",
			             true,
			             false,
			             {{"Scene", true, false, {}}, {"Project", false, false, {}}}},
			            {"Open...", true, false, {}},
			            {"Save", true, false, {}},
			            {"Save As...", true, false, {}}}},
			    {"Edit",
			        true,
			        false,
			        {{"Undo", false, false, {}},
			            {"Redo", false, false, {}},
			            {"Cut", false, false, {}},
			            {"Copy", false, false, {}}}},
			    {"View",
			        true,
			        false,
			        {{"Hierarchy", false, false, {}},
			            {"Inspector", false, false, {}},
			            {"Console", false, false, {}},
			            {"Reset Layout", true, false, {}}}},
			    {"Build",
			        true,
			        false,
			        {{"Build Project", false, false, {}},
			            {"Rebuild", false, false, {}},
			            {"Clean", false, false, {}},
			            {"Settings", false, false, {}}}},
			    {"Help",
			        true,
			        false,
			        {{"Documentation", false, false, {}},
			            {"Shortcuts", false, false, {}},
			            {"Report Issue", false, false, {}},
			            {"About", true, false, {}}}}
			};

			root.AddChild(fyuu_ui::Border{m_theme.background});

			auto main = root.AddChild(
			    fyuu_ui::SplitView{
			        fyuu_ui::Orientation::Horizontal,
			        0.19f,
			        220.0f,
			        620.0f,
			        8.0f,
			        true
			    }
			);
			m_main_split_node = main.GetID();
			fyuu_ui::LayoutProperties main_layout;
			main_layout.margin = {8.0f, 36.0f, 8.0f, 34.0f};
			main.SetLayout(main_layout);

			auto hierarchy = main.AddChild(fyuu_ui::Overlay{true});
			hierarchy.AddChild(fyuu_ui::Border{m_theme.panel});
			auto hierarchy_header = hierarchy.AddChild(fyuu_ui::Border{m_theme.raised_surface});
			hierarchy_header.SetLayout(FixedHeight(40.0f, fyuu_ui::Alignment::Start));
			auto hierarchy_title = hierarchy_header.AddChild(
				fyuu_ui::TextBlock{"Scene hierarchy", m_theme.text}
			);
			fyuu_ui::LayoutProperties panel_title_layout;
			panel_title_layout.margin.left = 14.0f;
			panel_title_layout.vertical_alignment = fyuu_ui::Alignment::Center;
			hierarchy_title.SetLayout(panel_title_layout);

			auto workspace = main.AddChild(
			    fyuu_ui::SplitView{
			        fyuu_ui::Orientation::Horizontal,
			        0.74f,
			        520.0f,
			        280.0f,
			        8.0f,
			        true
			    }
			);
			m_workspace_split_node = workspace.GetID();
			auto scene = workspace.AddChild(fyuu_ui::Overlay{true});
			scene.AddChild(fyuu_ui::Border{m_theme.panel});
			auto scene_header = scene.AddChild(fyuu_ui::Border{m_theme.raised_surface});
			scene_header.SetLayout(FixedHeight(40.0f, fyuu_ui::Alignment::Start));
			auto scene_header_content = scene_header.AddChild(fyuu_ui::Overlay{});
			fyuu_ui::LayoutProperties scene_header_content_layout;
			scene_header_content_layout.margin = {14.0f, 0.0f, 14.0f, 0.0f};
			scene_header_content.SetLayout(scene_header_content_layout);
			auto scene_title = scene_header_content.AddChild(
				fyuu_ui::TextBlock{"Scene", m_theme.text}
			);
			scene_title.SetLayout(FixedHeight(40.0f, fyuu_ui::Alignment::Center));
			auto scene_view = scene.AddChild(fyuu_ui::SceneView{m_theme.background});
			fyuu_ui::LayoutProperties scene_view_layout;
			scene_view_layout.margin = {1.0f, 41.0f, 1.0f, 1.0f};
			scene_view.SetLayout(scene_view_layout);
			m_scene_view_node = scene_view.GetID();

			auto inspector = workspace.AddChild(fyuu_ui::Overlay{true});
			inspector.AddChild(fyuu_ui::Border{m_theme.panel});
			auto inspector_header = inspector.AddChild(fyuu_ui::Border{m_theme.raised_surface});
			inspector_header.SetLayout(FixedHeight(40.0f, fyuu_ui::Alignment::Start));
			auto inspector_title = inspector_header.AddChild(
				fyuu_ui::TextBlock{"Inspector", m_theme.text}
			);
			inspector_title.SetLayout(panel_title_layout);

			auto status = root.AddChild(fyuu_ui::Border{m_theme.panel});
			status.SetLayout(FixedHeight(26.0f, fyuu_ui::Alignment::End));
			auto status_text = status.AddChild(
			    fyuu_ui::TextBlock{
			        std::format("Ready    {}", backend_name),
			        m_theme.muted_text
			    }
			);
			m_status_node = status_text.GetID();

			// MenuBar emits its popup panels inline, so it must sit after the main
			// content (and status bar) in the root's child order to draw on top of
			// them; window_layer above lets floating windows cover the bar row.
			auto menu_bar = root.AddChild(
				fyuu_ui::MenuBar{PathText(m_project_path.stem()), std::move(menu_entries)}
			);
			m_menu_bar_node = menu_bar.GetID();
			menu_bar.SetLayout(FixedHeight(28.0f, fyuu_ui::Alignment::Start));
			m_subscriptions.emplace_back(m_events.Subscribe<fyuu_ui::MenuActivatedEvent>(menu_bar,
			    [this](fyuu_ui::MenuActivatedEvent& event) {
				    ActivateMenuPath(event.path);
				    event.handled = true;
			    }
			));

			m_tree.GetNode(m_window_layer_node).BringToFront();
		}

	public:
		StudioUI(std::uint32_t width, std::uint32_t height, std::string_view backend_name) :
		    m_theme(fyuu_ui::DarkTheme()), m_tree(fyuu_ui::Overlay{}),
		    m_events(m_tree.BuildEventBus()), m_dialog_host(m_tree, m_events),
		    m_error_box(m_dialog_host),
		    m_backend_name(backend_name),
		    m_logical_width(width), m_logical_height(height), m_pixel_width(width),
		    m_pixel_height(height) {
			BuildStartupPage();
			m_file_dialog.emplace(m_dialog_host);
		}

		void EnterProject(
			std::filesystem::path const& path,
			std::filesystem::path const& asset_path
		) {
			m_project_path = path;
			m_asset_path = asset_path;
			m_recent_projects.Touch(path);
			m_events.Remove(m_tree, m_page_node);
			m_page_node = 0u;
			m_open_project_node = 0u;
			m_new_project_node = 0u;
			// LogicalTree deliberately reuses empty slots. Drop every ID owned by the
			// removed page before editor construction can assign those slots to controls
			// of different types.
			m_recent_project_nodes.clear();
			BuildEditorShell(m_backend_name);
			m_status_context = std::format("Project: {}", PathText(path));
			UpdateStatus();
		}

		void SetSceneStatus(std::string_view status) {
			m_status_context = status;
			UpdateStatus();
		}

		void ShowSceneError(std::string_view message) {
			m_status_context = message;
			UpdateStatus();
			m_error_box.Show("Scene error", message);
		}

		void DrainCommands(std::vector<StudioCommand>& output) {
			output.clear();
			output.swap(m_commands);
		}

		void ProcessEvent(fyuu_desktop::Event const& event) override {
			// EventBus owns focus. Rebuilding or closing a dialog can transfer it while
			// invalidating the previous logical node, so never retain an older mirror.
			auto const focused = m_events.FocusedNodeIDs();
			m_focused_node = focused.empty() ? std::nullopt :
			                                   std::optional<std::uint64_t>{focused.front()};
			if (event.type == fyuu_desktop::EventType::KeyPressed ||
			    event.type == fyuu_desktop::EventType::KeyReleased) {
				auto const key = static_cast<fyuu_ui::Key>(event.key);
				if (event.type == fyuu_desktop::EventType::KeyPressed) {
					fyuu_ui::KeyDownEvent input{
					    key, event.shift, event.control, event.alt, false
					};
					if (DispatchFocused(input))
						return;
				} else {
					fyuu_ui::KeyUpEvent input{
					    key, event.shift, event.control, event.alt, false
					};
					DispatchFocused(input);
					return;
				}
			}
			if (event.type == fyuu_desktop::EventType::TextInput) {
				fyuu_ui::TextInputEvent input{event.text, false};
				if (DispatchFocused(input))
					return;
			}
			if (EditFocusedTextBox(event)) {
				return;
			}
			if (m_dialog_host.IsOpen() &&
			    (event.type == fyuu_desktop::EventType::KeyPressed ||
			        event.type == fyuu_desktop::EventType::TextInput)) {
				return;
			}
			if (event.type == fyuu_desktop::EventType::WindowResized) {
				m_logical_width = static_cast<std::uint32_t>((std::max)(event.x, 0.0f));
				m_logical_height = static_cast<std::uint32_t>((std::max)(event.y, 0.0f));
				return;
			}
			if (event.type == fyuu_desktop::EventType::WindowPixelSizeChanged) {
				m_pixel_width = static_cast<std::uint32_t>((std::max)(event.x, 0.0f));
				m_pixel_height = static_cast<std::uint32_t>((std::max)(event.y, 0.0f));
				return;
			}
			if (event.type == fyuu_desktop::EventType::WindowMouseLeave) {
				ClearPointerStates();
				CloseMenu();
				return;
			}
			if (event.type == fyuu_desktop::EventType::KeyPressed &&
			    event.key == fyuu_desktop::Key::Tab) {
				m_events.MoveFocus(m_tree, event.shift ? fyuu_ui::FocusDirection::Previous :
				                             fyuu_ui::FocusDirection::Next);
				auto const tab_focus = m_events.FocusedNodeIDs();
				if (tab_focus.empty())
					m_focused_node.reset();
				else
					m_focused_node = tab_focus.front();
				return;
			}
			if (event.type == fyuu_desktop::EventType::MouseMoved) {
				m_pointer_x = event.x;
				m_pointer_y = event.y;
				if (m_pressed_node && m_text_drag_bounds && IsTextBoxNode(*m_pressed_node)) {
					PositionTextCaret(
					    *m_pressed_node, event.x - m_text_drag_bounds->position.x, true, false
					);
				}
				fyuu_ui::PointerMovedEvent input{{event.x, event.y}, false};
				if (DispatchPointer(input, input.position))
					return;
				UpdatePointerState(event.x, event.y);
				if (m_scroll_bar_node && m_tree.IsInSubtree(0u, *m_scroll_bar_node)) {
					if (auto* view = m_tree.GetNode(*m_scroll_bar_node)
					                     .TryAsContainer<fyuu_ui::ScrollView>();
					    view != nullptr && fyuu_ui::DragScrollBar(*view, event.y)) return;
				}
				if (m_window_drag_node) {
					UpdateWindowPosition(event.x, event.y);
				} else if (m_window_resize_node) {
					UpdateWindowSize(event.x, event.y);
				} else if (m_split_drag_node) {
					UpdateSplit(event.x);
				}
				return;
			}
			if (event.type == fyuu_desktop::EventType::MouseWheel) {
				auto tree = BuildVisualTree();
				auto const hit = tree.HitTest({m_pointer_x, m_pointer_y});
				if (m_dialog_host.IsOpen() &&
				    (!hit || !m_dialog_host.AllowsInput(hit->logical_id))) {
					return;
				}
				if (hit && Scroll(hit->logical_id, event.delta_y)) {
					return;
				}
				return;
			}
			if ((event.type == fyuu_desktop::EventType::MouseButtonPressed ||
			        event.type == fyuu_desktop::EventType::MouseButtonReleased) &&
			    event.mouse_button != fyuu_desktop::MouseButton::Left) {
				fyuu_ui::PointerPressedEvent pressed{
				    {event.x, event.y}, ToPointerButton(event.mouse_button), event.click_count, false
				};
				fyuu_ui::PointerReleasedEvent released{
				    {event.x, event.y}, ToPointerButton(event.mouse_button), false
				};
				if (event.type == fyuu_desktop::EventType::MouseButtonPressed)
					DispatchPointer(pressed, pressed.position);
				else {
					DispatchPointer(released, released.position);
					m_events.ReleasePointer();
				}
				return;
			}
			auto visual_tree = BuildVisualTree();
			if (event.type == fyuu_desktop::EventType::MouseButtonPressed) {
				m_pointer_x = event.x;
				m_pointer_y = event.y;
				auto const hit = visual_tree.HitTest({event.x, event.y});
				if (m_dialog_host.IsOpen() &&
				    (!hit || !m_dialog_host.AllowsInput(hit->logical_id))) {
					m_pressed_node.reset();
					ClearPointerStates();
					return;
				}
				FocusWindow(event.x, event.y);
				if (!hit) {
					m_pressed_node.reset();
					ClearPointerStates();
					CloseMenu();
					return;
				}
				fyuu_ui::PointerPressedEvent input{
				    {event.x, event.y}, fyuu_ui::PointerButton::Left, event.click_count, false
				};
				m_events.Dispatch(m_tree, m_tree.GetNode(hit->logical_id), input);
				if (input.handled)
					return;
				m_events.CapturePointer(m_tree, m_tree.GetNode(hit->logical_id));
				if (auto* view = m_tree.GetNode(hit->logical_id)
				                     .TryAsContainer<fyuu_ui::ScrollView>();
				    view != nullptr && fyuu_ui::BeginScrollBarDrag(
				        *view,
				        {event.x, event.y},
				        hit->position,
				        hit->size
				    )) {
					m_scroll_bar_node = hit->logical_id;
					m_pressed_node = hit->logical_id;
					CloseMenu();
					ClearPointerStates();
					SetPointerState(hit->logical_id, fyuu_ui::InteractionState::Pressed);
					return;
				}
				if (BeginWindowInteraction(*hit)) {
					CloseMenu();
					return;
				}
				if (m_menu_bar_node != 0u && hit->logical_id != m_menu_bar_node) {
					CloseMenu();
				}
				if (m_events.Focus(m_tree, m_tree.GetNode(hit->logical_id))) {
					m_focused_node = hit->logical_id;
				} else {
					m_events.ClearFocus(m_tree);
					m_focused_node.reset();
				}
				if (IsTextBoxNode(hit->logical_id)) {
					m_text_drag_bounds = fyuu_ui::Rect{
					    {event.x - hit->position.x, event.y - hit->position.y}, hit->size
					};
					PositionTextCaret(
					    hit->logical_id, hit->position.x, event.shift, event.click_count >= 2u
					);
				} else {
					m_text_drag_bounds.reset();
				}
				m_pressed_node = hit->logical_id;
				// BeginWindowInteraction stores a non-client role and returns above.
				// Every remaining hit is ordinary content and must clear that role;
				// otherwise pointer motion can later cast a Button node to Window.
				m_pressed_hit_role = fyuu_ui::HitTestRole::Content;
				ClearPointerStates();
				SetPointerState(hit->logical_id, fyuu_ui::InteractionState::Pressed);
				if (m_menu_bar_node != 0u && hit->logical_id == m_menu_bar_node) {
					if (!hit->menu_path.indices.empty()) {
						m_pressed_menu_path = hit->menu_path;
						m_tree.GetNode(m_menu_bar_node).AsWidget<fyuu_ui::MenuBar>().pressed_path =
						    hit->menu_path.indices;
					} else {
						CloseMenu();
					}
				}
				if (m_file_dialog->OwnsTextBox(hit->logical_id)) {
					m_file_dialog->SetTextBoxFocused(hit->logical_id, true);
					m_focused_node = hit->logical_id;
				} else {
					if (m_focused_node && m_file_dialog->OwnsTextBox(*m_focused_node)) {
						m_file_dialog->SetTextBoxFocused(*m_focused_node, false);
						m_focused_node.reset();
					}
				}
				if (
				    (m_main_split_node != 0u && hit->logical_id == m_main_split_node) ||
				    (m_workspace_split_node != 0u && hit->logical_id == m_workspace_split_node)
				) {
					auto& split = m_tree.GetNode(hit->logical_id).AsContainer<fyuu_ui::SplitView>();
					m_split_drag_node = hit->logical_id;
					m_split_drag_start = event.x;
					m_split_drag_value = split.split;
					if (hit->logical_id == m_main_split_node) {
						m_split_drag_extent =
						    (std::max)(1.0f, static_cast<float>(m_logical_width) - split.spacing);
					} else {
						auto const& main_split =
						    m_tree.GetNode(m_main_split_node).AsContainer<fyuu_ui::SplitView>();
						m_split_drag_extent = (std::max)(1.0f,
						    static_cast<float>(m_logical_width) * (1.0f - main_split.split) -
						        split.spacing);
					}
				}
				return;
			}
			if (event.type != fyuu_desktop::EventType::MouseButtonReleased) {
				return;
			}
			fyuu_ui::PointerReleasedEvent input{
			    {event.x, event.y}, fyuu_ui::PointerButton::Left, false
			};
			DispatchPointer(input, input.position);
			m_events.ReleasePointer();
			m_text_drag_bounds.reset();
			auto* scroll_view = m_scroll_bar_node && m_tree.IsInSubtree(0u, *m_scroll_bar_node) ?
			    m_tree.GetNode(*m_scroll_bar_node).TryAsContainer<fyuu_ui::ScrollView>() :
			    nullptr;
			if (scroll_view != nullptr && fyuu_ui::EndScrollBarDrag(*scroll_view)) {
				m_pressed_node.reset();
				m_scroll_bar_node.reset();
				UpdatePointerState(event.x, event.y);
				return;
			}
			if (m_window_drag_node) {
				m_window_drag_node.reset();
				m_pressed_node.reset();
				m_pressed_hit_role = fyuu_ui::HitTestRole::Content;
				UpdatePointerState(event.x, event.y);
				return;
			}
			if (m_window_resize_node) {
				m_window_resize_node.reset();
				m_window_resize_region = fyuu_ui::WindowResizeRegion::None;
				m_pressed_node.reset();
				m_pressed_hit_role = fyuu_ui::HitTestRole::Content;
				UpdatePointerState(event.x, event.y);
				return;
			}
			m_split_drag_node.reset();
			auto const released = visual_tree.HitTest({event.x, event.y});
			if (!m_pressed_menu_path.indices.empty()) {
				auto& bar = m_tree.GetNode(m_menu_bar_node).AsWidget<fyuu_ui::MenuBar>();
				auto const press = m_pressed_menu_path;
				m_pressed_menu_path.indices.clear();
				bar.pressed_path.clear();
				auto const on_same_entry = released && released->logical_id == m_menu_bar_node &&
				    !released->menu_path.indices.empty() &&
				    released->menu_path.indices == press.indices;
				if (on_same_entry) {
					auto const* entry = fyuu_ui::FindMenuEntry(bar, press.indices);
					if (entry != nullptr && !entry->children.empty()) {
						// Submenu header: toggle its dropdown / cascade.
						if (bar.open_path == press.indices) {
							bar.open_path.clear();
						} else {
							bar.open_path = press.indices;
						}
					} else {
						fyuu_ui::MenuActivatedEvent activated{press};
						m_events.Dispatch(m_tree, m_tree.GetNode(m_menu_bar_node), activated);
					}
				} else if (
				    released && released->logical_id == m_menu_bar_node &&
				    !released->menu_path.indices.empty()
				) {
					auto const& path = released->menu_path.indices;
					if (path.size() == 1u) {
						// Drag released on another bar item: switch the open menu.
						bar.open_path = path;
					} else {
						auto const* entry = fyuu_ui::FindMenuEntry(bar, path);
						if (entry != nullptr && !entry->children.empty()) {
							bar.open_path = path;
						}
					}
				}
			} else if (released && released->logical_id == m_pressed_node) {
				auto node = m_tree.GetNode(released->logical_id);
				fyuu_ui::ClickEvent click{{event.x, event.y}};
				m_events.Dispatch(m_tree, node, click);
			}
			m_file_dialog->Update();
			m_error_box.Update();
			m_pressed_node.reset();
			m_pressed_hit_role = fyuu_ui::HitTestRole::Content;
			UpdatePointerState(event.x, event.y);
		}

		std::vector<fyuu_ui::DrawCommand> BuildDrawList() {
			auto tree = BuildVisualTree();
			return tree.WriteDrawList();
		}

		std::uint32_t GetLogicalWidth() const noexcept {
			return m_logical_width;
		}

		std::uint32_t GetLogicalHeight() const noexcept {
			return m_logical_height;
		}

		std::uint32_t GetPixelWidth() const noexcept {
			return m_pixel_width;
		}

		std::uint32_t GetPixelHeight() const noexcept {
			return m_pixel_height;
		}
	};

} // namespace fyuu_studio
