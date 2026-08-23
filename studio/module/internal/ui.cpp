module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <format>
#endif // !defined(__cpp_lib_modules)

module fyuu_studio:ui;

import fyuu_desktop;
import fyuu_ui;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace fyuu_studio {

	enum class StudioCommand {
		DocumentEdited,
		NewDocument,
		SaveDocument,
		SaveAndClose,
		DiscardAndClose
	};

	class StudioUI final : public fyuu_desktop::EventSink {
	private:
		fyuu_ui::Theme m_theme;
		fyuu_ui::LogicalTree m_tree;
		std::vector<StudioCommand> m_commands;
		std::string m_name_before_edit;
		std::string m_backend_name;
		std::string m_status_context = "Ready";
		std::string m_document_title = "Untitled Scene";
		std::optional<std::uint64_t> m_pressed_node;
		std::optional<std::uint64_t> m_focused_node;
		std::optional<std::uint64_t> m_split_drag_node;
		std::optional<std::uint64_t> m_numeric_drag_node;
		std::optional<std::uint64_t> m_about_window_node;
		std::optional<std::uint64_t> m_window_drag_node;
		std::optional<std::uint64_t> m_window_resize_node;
		std::optional<fyuu_ui::Rect> m_drag_bounds;
		fyuu_ui::HitTestRole m_pressed_hit_role = fyuu_ui::HitTestRole::Content;
		fyuu_ui::WindowResizeRegion m_window_resize_region = fyuu_ui::WindowResizeRegion::None;
		bool m_camera_visible = true;
		bool m_light_visible = true;
		bool m_document_dirty = false;
		bool m_close_confirmation_open = false;
		std::uint64_t m_document_revision = 0u;
		std::uint64_t m_selected_entity_node = 0u;
		std::uint64_t m_camera_node = 0u;
		std::uint64_t m_light_node = 0u;
		std::uint64_t m_inspector_name_node = 0u;
		std::uint64_t m_inspector_visible_node = 0u;
		std::uint64_t m_translate_node = 0u;
		std::uint64_t m_rotate_node = 0u;
		std::uint64_t m_scale_node = 0u;
		std::uint64_t m_slider_node = 0u;
		std::uint64_t m_main_split_node = 0u;
		std::uint64_t m_workspace_split_node = 0u;
		std::uint64_t m_scene_view_node = 0u;
		std::uint64_t m_status_node = 0u;
		std::uint64_t m_menu_bar_node = 0u;
		std::optional<fyuu_ui::MenuPath> m_pressed_menu_path;
		std::uint64_t m_window_layer_node = 0u;
		std::uint64_t m_close_confirmation_node = 0u;
		std::uint64_t m_close_dialog_node = 0u;
		std::uint64_t m_close_save_node = 0u;
		std::uint64_t m_close_discard_node = 0u;
		std::uint64_t m_close_cancel_node = 0u;
		std::uint64_t m_position_x_node = 0u;
		std::uint64_t m_position_y_node = 0u;
		std::uint64_t m_position_z_node = 0u;
		std::uint32_t m_logical_width;
		std::uint32_t m_logical_height;
		std::uint32_t m_pixel_width;
		std::uint32_t m_pixel_height;
		float m_pointer_x = 0.0f;
		float m_pointer_y = 0.0f;
		float m_split_drag_start = 0.0f;
		float m_split_drag_value = 0.0f;
		float m_split_drag_extent = 1.0f;
		float m_numeric_drag_start = 0.0f;
		float m_window_drag_offset_x = 0.0f;
		float m_window_drag_offset_y = 0.0f;
		fyuu_ui::Point m_window_resize_start;
		fyuu_ui::Point m_window_resize_position;
		fyuu_ui::Size m_window_resize_size;
		double m_numeric_drag_value = 0.0;

		static fyuu_ui::LayoutProperties FixedHeight(float height, fyuu_ui::Alignment alignment) {
			fyuu_ui::LayoutProperties result;
			result.height = height;
			result.vertical_alignment = alignment;
			return result;
		}

		void UpdateZoomStatus() {
			auto const& slider = m_tree.GetNode(m_slider_node).GetWidget<fyuu_ui::Slider>();
			auto& status = m_tree.GetNode(m_status_node).GetWidget<fyuu_ui::TextBlock>();
			status.text = std::format(
				"{}    {}    Zoom {:.0f}%    {}{}    Revision {}",
				m_status_context,
				m_backend_name,
				slider.value * 100.0f,
				m_document_title,
				m_document_dirty ? " *" : "",
				m_document_revision
			);
		}

		void UpdateSlider(float x, float y) {
			if (!m_drag_bounds.has_value()) {
				return;
			}
			auto& slider = m_tree.GetNode(m_slider_node).GetWidget<fyuu_ui::Slider>();
			auto ratio = 0.0f;
			if (slider.orientation == fyuu_ui::Orientation::Horizontal &&
				m_drag_bounds->size.width > 0.0f) {
				ratio = (x - m_drag_bounds->position.x) / m_drag_bounds->size.width;
			}
			else if (m_drag_bounds->size.height > 0.0f) {
				ratio = 1.0f - (y - m_drag_bounds->position.y) / m_drag_bounds->size.height;
			}
			ratio = std::clamp(ratio, 0.0f, 1.0f);
			slider.value = slider.minimum + (slider.maximum - slider.minimum) * ratio;
			if (slider.step > 0.0f) {
				slider.value = slider.minimum +
					std::round((slider.value - slider.minimum) / slider.step) * slider.step;
			}
			UpdateZoomStatus();
		}

		void AdjustSlider(float direction) {
			auto& slider = m_tree.GetNode(m_slider_node).GetWidget<fyuu_ui::Slider>();
			auto const step = slider.step > 0.0f
				? slider.step
				: (slider.maximum - slider.minimum) * 0.01f;
			slider.value = std::clamp(
				slider.value + step * direction,
				slider.minimum,
				slider.maximum
			);
			UpdateZoomStatus();
		}

		void UpdateSplit(float x) {
			if (!m_split_drag_node.has_value()) {
				return;
			}
			auto& split = m_tree.GetNode(*m_split_drag_node).GetContainer<fyuu_ui::SplitView>();
			split.split = std::clamp(
				m_split_drag_value + (x - m_split_drag_start) / m_split_drag_extent,
				0.0f,
				1.0f
			);
		}

		bool IsNumericNode(std::uint64_t node) const noexcept {
			return node == m_position_x_node ||
				node == m_position_y_node ||
				node == m_position_z_node;
		}

		void SetPointerState(std::uint64_t node, fyuu_ui::InteractionState state) {
			if (node == m_camera_node || node == m_light_node ||
				node == m_translate_node || node == m_rotate_node ||
				node == m_scale_node) {
				m_tree.GetNode(node).GetWidget<fyuu_ui::ToggleButton>().interaction = state;
			}
			else if (node == m_close_save_node || node == m_close_discard_node ||
				node == m_close_cancel_node) {
				m_tree.GetNode(node).GetWidget<fyuu_ui::Button>().interaction = state;
			}
			else if (node == m_inspector_visible_node) {
				m_tree.GetNode(node).GetWidget<fyuu_ui::CheckBox>().interaction = state;
			}
			else if (node == m_slider_node) {
				m_tree.GetNode(node).GetWidget<fyuu_ui::Slider>().interaction = state;
			}
			else if (IsNumericNode(node)) {
				m_tree.GetNode(node).GetWidget<fyuu_ui::NumericBox>().interaction = state;
			}
		}

		void ClearPointerStates() {
			if (m_about_window_node.has_value()) {
				auto& window = m_tree.GetNode(*m_about_window_node).GetWidget<fyuu_ui::Window>();
				window.non_client_button_interaction = fyuu_ui::InteractionState::Normal;
			}
			SetPointerState(m_close_save_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_close_discard_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_close_cancel_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_camera_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_light_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_translate_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_rotate_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_scale_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_inspector_visible_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_slider_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_position_x_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_position_y_node, fyuu_ui::InteractionState::Normal);
			SetPointerState(m_position_z_node, fyuu_ui::InteractionState::Normal);
		}

		void UpdatePointerState(float x, float y) {
			ClearPointerStates();
			if (m_pressed_node.has_value()) {
				if (m_about_window_node == m_pressed_node) {
					auto& window = m_tree.GetNode(*m_about_window_node).GetWidget<fyuu_ui::Window>();
					if (m_pressed_hit_role == fyuu_ui::HitTestRole::WindowNonClientButton) {
						window.non_client_button_interaction = fyuu_ui::InteractionState::Pressed;
					}
				}
				SetPointerState(*m_pressed_node, fyuu_ui::InteractionState::Pressed);
				return;
			}
			auto tree = m_tree.BuildVisualTree(
				{ static_cast<float>(m_logical_width), static_cast<float>(m_logical_height) },
				m_theme
			);
			auto const hit = tree.HitTest({ x, y });
			if (hit.has_value()) {
				SetPointerState(hit->logical_id, fyuu_ui::InteractionState::Hovered);
				if (hit->logical_id == m_menu_bar_node) {
					SetMenuHover(hit->menu_path);
				}
				else {
					auto& bar = m_tree.GetNode(m_menu_bar_node).GetWidget<fyuu_ui::MenuBar>();
					bar.hover_path.reset();
				}
				if (m_about_window_node == hit->logical_id) {
					auto& window = m_tree.GetNode(*m_about_window_node).GetWidget<fyuu_ui::Window>();
					if (hit->role == fyuu_ui::HitTestRole::WindowNonClientButton) {
						window.non_client_button_interaction = fyuu_ui::InteractionState::Hovered;
					}
				}
			}
		}

		void AdjustNumericBox(
			std::uint64_t node,
			double direction,
			bool fine,
			bool coarse
		) {
			auto& numeric = m_tree.GetNode(node).GetWidget<fyuu_ui::NumericBox>();
			if (numeric.read_only) {
				return;
			}
			auto step = numeric.step;
			if (fine) {
				step *= 0.1;
			}
			else if (coarse) {
				step *= 10.0;
			}
			numeric.value = std::clamp(
				numeric.value + direction * step,
				numeric.minimum,
				numeric.maximum
			);
			m_commands.emplace_back(StudioCommand::DocumentEdited);
		}

		void UpdateNumericBox(float x) {
			if (!m_numeric_drag_node.has_value()) {
				return;
			}
			auto& numeric = m_tree.GetNode(*m_numeric_drag_node).GetWidget<fyuu_ui::NumericBox>();
			auto value = m_numeric_drag_value +
				static_cast<double>(x - m_numeric_drag_start) * numeric.step * 0.25;
			if (numeric.step > 0.0) {
				value = std::round(value / numeric.step) * numeric.step;
			}
			numeric.value = std::clamp(value, numeric.minimum, numeric.maximum);
			m_commands.emplace_back(StudioCommand::DocumentEdited);
		}

		void SelectEntity(
			std::uint64_t selected_node,
			std::uint64_t other_node
		) {
			m_tree.GetNode(selected_node).GetWidget<fyuu_ui::ToggleButton>().checked = true;
			m_tree.GetNode(other_node).GetWidget<fyuu_ui::ToggleButton>().checked = false;
			m_selected_entity_node = selected_node;
			auto const& selected = m_tree.GetNode(selected_node).GetWidget<fyuu_ui::ToggleButton>();
			m_tree.GetNode(m_inspector_name_node).GetWidget<fyuu_ui::TextBox>().text = selected.title;
			m_tree.GetNode(m_inspector_visible_node).GetWidget<fyuu_ui::CheckBox>().checked =
				selected_node == m_camera_node ? m_camera_visible : m_light_visible;
		}

		static std::size_t PreviousCodePoint(std::string_view text, std::size_t offset) {
			if (offset == 0u) {
				return 0u;
			}
			--offset;
			while (offset != 0u &&
				(static_cast<unsigned char>(text[offset]) & 0xC0u) == 0x80u) {
				--offset;
			}
			return offset;
		}

		static std::size_t NextCodePoint(std::string_view text, std::size_t offset) {
			if (offset >= text.size()) {
				return text.size();
			}
			++offset;
			while (offset < text.size() &&
				(static_cast<unsigned char>(text[offset]) & 0xC0u) == 0x80u) {
				++offset;
			}
			return offset;
		}

		void InsertText(std::string_view input) {
			auto& text_box = m_tree.GetNode(m_inspector_name_node).GetWidget<fyuu_ui::TextBox>();
			text_box.text.insert(text_box.caret_offset, input);
			text_box.caret_offset += input.size();
			auto const& name = m_tree.GetNode(m_inspector_name_node).GetWidget<fyuu_ui::TextBox>().text;
m_tree.GetNode(m_selected_entity_node).GetWidget<fyuu_ui::ToggleButton>().title = name;
			m_commands.emplace_back(StudioCommand::DocumentEdited);
		}

		void ErasePreviousCodePoint() {
			auto& text_box = m_tree.GetNode(m_inspector_name_node).GetWidget<fyuu_ui::TextBox>();
			if (text_box.caret_offset == 0u) {
				return;
			}
			auto const previous = PreviousCodePoint(text_box.text, text_box.caret_offset);
			text_box.text.erase(previous, text_box.caret_offset - previous);
			text_box.caret_offset = previous;
			auto const& name = m_tree.GetNode(m_inspector_name_node).GetWidget<fyuu_ui::TextBox>().text;
m_tree.GetNode(m_selected_entity_node).GetWidget<fyuu_ui::ToggleButton>().title = name;
			m_commands.emplace_back(StudioCommand::DocumentEdited);
		}

		void EraseNextCodePoint() {
			auto& text_box = m_tree.GetNode(m_inspector_name_node).GetWidget<fyuu_ui::TextBox>();
			if (text_box.caret_offset == text_box.text.size()) {
				return;
			}
			auto const next = NextCodePoint(text_box.text, text_box.caret_offset);
			text_box.text.erase(text_box.caret_offset, next - text_box.caret_offset);
			auto const& name = m_tree.GetNode(m_inspector_name_node).GetWidget<fyuu_ui::TextBox>().text;
m_tree.GetNode(m_selected_entity_node).GetWidget<fyuu_ui::ToggleButton>().title = name;
			m_commands.emplace_back(StudioCommand::DocumentEdited);
		}

		void BeginNameEdit() {
			if (m_focused_node == m_inspector_name_node) {
				return;
			}
			auto& text_box = m_tree.GetNode(m_inspector_name_node).GetWidget<fyuu_ui::TextBox>();
			m_name_before_edit = text_box.text;
			text_box.focused = true;
			text_box.caret_offset = text_box.text.size();
			m_focused_node = m_inspector_name_node;
		}

		void EndNameEdit(bool commit) {
			if (m_focused_node != m_inspector_name_node) {
				return;
			}
			auto& text_box = m_tree.GetNode(m_inspector_name_node).GetWidget<fyuu_ui::TextBox>();
			if (!commit) {
				text_box.text = m_name_before_edit;
				auto const& name = m_tree.GetNode(m_inspector_name_node).GetWidget<fyuu_ui::TextBox>().text;
m_tree.GetNode(m_selected_entity_node).GetWidget<fyuu_ui::ToggleButton>().title = name;
			}
			text_box.focused = false;
			m_focused_node.reset();
		}

		void ToggleSelectedVisibility() {
			if (m_selected_entity_node == m_camera_node) {
				m_camera_visible = !m_camera_visible;
				m_tree.GetNode(m_inspector_visible_node).GetWidget<fyuu_ui::CheckBox>().checked =
					m_camera_visible;
				m_commands.emplace_back(StudioCommand::DocumentEdited);
				return;
			}
			m_light_visible = !m_light_visible;
			m_tree.GetNode(m_inspector_visible_node).GetWidget<fyuu_ui::CheckBox>().checked =
				m_light_visible;
			m_commands.emplace_back(StudioCommand::DocumentEdited);
		}

		void CloseMenu() {
			auto& bar = m_tree.GetNode(m_menu_bar_node).GetWidget<fyuu_ui::MenuBar>();
			bar.open_path.reset();
			bar.hover_path.reset();
			bar.pressed_path.reset();
		}

		void SetMenuHover(std::optional<fyuu_ui::MenuPath> const& path) {
			auto& bar = m_tree.GetNode(m_menu_bar_node).GetWidget<fyuu_ui::MenuBar>();
			if (!path.has_value()) {
				bar.hover_path.reset();
				return;
			}
			auto const& p = path->indices;
			bar.hover_path = p;
			if (p.size() == 1u && bar.open_path.has_value() &&
				(*bar.open_path)[0u] != p[0u] && !bar.entries[p[0u]].children.empty()) {
				bar.open_path = p;
				m_status_context = std::format("{} menu", bar.entries[p[0u]].title);
				UpdateZoomStatus();
				return;
			}
			if (p.size() >= 2u && bar.open_path.has_value()) {
				auto const* entry = fyuu_ui::FindMenuEntry(bar, p);
				if (entry != nullptr && !entry->children.empty() && *bar.open_path != p) {
					bar.open_path = p;
				}
			}
		}


		void ShowAboutWindow() {
			// Help -> About calls this function through ActivateMenuPath. The first
			// activation inserts the window and its retained content into WindowLayer;
			// later activations reuse the same logical subtree and only move it above
			// its siblings. BuildVisualTree observes that new sibling order next frame.
			if (m_about_window_node.has_value()) {
				auto window = m_tree.GetNode(*m_about_window_node);
				window.BringToFront();
				return;
			}
			auto layer = m_tree.GetNode(m_window_layer_node);
			auto window = layer.AddChild(fyuu_ui::Window{
				"About Fyuu Studio",
				{ 420.0f, 180.0f },
				{ 360.0f, 180.0f },
				true
			});
			m_about_window_node = window.GetID();
			window.Subscribe<fyuu_ui::ClickEvent>(
				[this](fyuu_ui::ClickEvent& event) {
					auto about = m_tree.GetNode(*m_about_window_node);
					about.BringToFront();
					event.handled = true;
				}
			);
			auto content = window.AddChild(
				fyuu_ui::StackPanel{ fyuu_ui::Orientation::Vertical, 8.0f }
			);
			fyuu_ui::LayoutProperties content_layout;
			content_layout.margin = { 16.0f, 12.0f, 16.0f, 12.0f };
			content.SetLayout(content_layout);
			content.AddChild(fyuu_ui::TextBlock{
				"Fyuu Studio",
				m_theme.window_client_text,
				16.0f
			});
			content.AddChild(fyuu_ui::TextBlock{
				"Built with FyuuUI",
				m_theme.window_client_muted_text,
				14.0f
			});
		}

		void UpdateWindowPosition(float x, float y) {
			if (!m_window_drag_node.has_value()) {
				return;
			}
			auto& window = m_tree.GetNode(*m_window_drag_node).GetWidget<fyuu_ui::Window>();
			window.position.x = std::max(0.0f, x - m_window_drag_offset_x);
			window.position.y = std::max(0.0f, y - m_window_drag_offset_y);
		}

		void UpdateWindowSize(float x, float y) {
			if (!m_window_resize_node.has_value()) {
				return;
			}
			auto& window = m_tree.GetNode(*m_window_resize_node).GetWidget<fyuu_ui::Window>();
			auto const delta_x = x - m_window_resize_start.x;
			auto const delta_y = y - m_window_resize_start.y;
			auto const resize_left = m_window_resize_region == fyuu_ui::WindowResizeRegion::Left ||
				m_window_resize_region == fyuu_ui::WindowResizeRegion::TopLeft ||
				m_window_resize_region == fyuu_ui::WindowResizeRegion::BottomLeft;
			auto const resize_top = m_window_resize_region == fyuu_ui::WindowResizeRegion::Top ||
				m_window_resize_region == fyuu_ui::WindowResizeRegion::TopLeft ||
				m_window_resize_region == fyuu_ui::WindowResizeRegion::TopRight;
			auto const resize_right = m_window_resize_region == fyuu_ui::WindowResizeRegion::Right ||
				m_window_resize_region == fyuu_ui::WindowResizeRegion::TopRight ||
				m_window_resize_region == fyuu_ui::WindowResizeRegion::BottomRight;
			auto const resize_bottom = m_window_resize_region == fyuu_ui::WindowResizeRegion::Bottom ||
				m_window_resize_region == fyuu_ui::WindowResizeRegion::BottomLeft ||
				m_window_resize_region == fyuu_ui::WindowResizeRegion::BottomRight;
			if (resize_left) {
				auto const maximum_x = m_window_resize_position.x +
					m_window_resize_size.width - window.minimum_size.width;
				window.position.x = std::clamp(
					m_window_resize_position.x + delta_x,
					0.0f,
					maximum_x
				);
				window.size.width = m_window_resize_size.width +
					m_window_resize_position.x - window.position.x;
			}
			else if (resize_right) {
				window.size.width = std::clamp(
					m_window_resize_size.width + delta_x,
					window.minimum_size.width,
					static_cast<float>(m_logical_width) - window.position.x
				);
			}
			if (resize_top) {
				auto const maximum_y = m_window_resize_position.y +
					m_window_resize_size.height - window.minimum_size.height;
				window.position.y = std::clamp(
					m_window_resize_position.y + delta_y,
					0.0f,
					maximum_y
				);
				window.size.height = m_window_resize_size.height +
					m_window_resize_position.y - window.position.y;
			}
			else if (resize_bottom) {
				window.size.height = std::clamp(
					m_window_resize_size.height + delta_y,
					window.minimum_size.height,
					static_cast<float>(m_logical_height) - window.position.y
				);
			}
		}

		void FocusWindow(float x, float y) {
			if (!m_about_window_node.has_value()) {
				return;
			}
			auto window = m_tree.GetNode(*m_about_window_node);
			auto& state = window.GetWidget<fyuu_ui::Window>();
			auto const inside = x >= state.position.x &&
				y >= state.position.y &&
				x < state.position.x + state.size.width &&
				y < state.position.y + state.size.height;
			if (inside) {
				window.BringToFront();
			}
			else {
				state.active = false;
			}
		}

		void CloseAboutWindow() {
			if (!m_about_window_node.has_value()) {
				return;
			}
			m_tree.Remove(*m_about_window_node);
			m_about_window_node.reset();
			m_window_drag_node.reset();
			m_window_resize_node.reset();
		}

		bool BeginWindowInteraction(fyuu_ui::HitTestResult const& hit) {
			if (!m_about_window_node.has_value() ||
				hit.logical_id != *m_about_window_node ||
				hit.role == fyuu_ui::HitTestRole::Content) {
				return false;
			}
			auto window = m_tree.GetNode(*m_about_window_node);
			window.BringToFront();
			auto const& state = window.GetWidget<fyuu_ui::Window>();
			m_pressed_node = *m_about_window_node;
			m_pressed_hit_role = hit.role;
			if (state.closable &&
				hit.role == fyuu_ui::HitTestRole::WindowNonClientButton) {
				CloseAboutWindow();
				m_pressed_node.reset();
				return true;
			}
			if (state.resizable &&
				hit.role == fyuu_ui::HitTestRole::WindowResize) {
				m_window_resize_node = *m_about_window_node;
				m_window_resize_region = hit.resize_region;
				m_window_resize_start = { m_pointer_x, m_pointer_y };
				m_window_resize_position = state.position;
				m_window_resize_size = state.size;
				return true;
			}
			m_window_drag_node = *m_about_window_node;
			m_window_drag_offset_x = hit.position.x;
			m_window_drag_offset_y = hit.position.y;
			return true;
		}

		void ActivateMenuPath(fyuu_ui::MenuPath const& path) {
			auto& bar = m_tree.GetNode(m_menu_bar_node).GetWidget<fyuu_ui::MenuBar>();
			auto const* entry = fyuu_ui::FindMenuEntry(bar, path.indices);
			if (entry == nullptr || !entry->enabled) {
				return;
			}
			m_status_context = entry->title;
			if (path.indices == std::vector<std::uint32_t>{ 0u, 0u, 0u }) {
				m_commands.emplace_back(StudioCommand::NewDocument);
			}
			else if (path.indices == std::vector<std::uint32_t>{ 0u, 2u }) {
				m_commands.emplace_back(StudioCommand::SaveDocument);
			}
			else if (path.indices == std::vector<std::uint32_t>{ 2u, 3u }) {
				auto& main = m_tree.GetNode(m_main_split_node).GetContainer<fyuu_ui::SplitView>();
				auto& workspace = m_tree.GetNode(m_workspace_split_node).GetContainer<fyuu_ui::SplitView>();
				main.split = 0.20f;
				workspace.split = 0.76f;
			}
			else if (path.indices == std::vector<std::uint32_t>{ 4u, 3u }) {
				ShowAboutWindow();
			}
			CloseMenu();
			UpdateZoomStatus();
		}

		void BuildEditorShell(std::string_view backend_name) {
			auto root = m_tree.GetRoot();
			std::vector<fyuu_ui::MenuEntry> menu_entries{
				{ "File", true, false, {
					{ "New", true, false, {
						{ "Scene", true, false, {} },
						{ "Project", false, false, {} }
					} },
					{ "Open...", false, false, {} },
					{ "Save", true, false, {} },
					{ "Save As...", false, false, {} }
				} },
				{ "Edit", true, false, {
					{ "Undo", false, false, {} },
					{ "Redo", false, false, {} },
					{ "Cut", false, false, {} },
					{ "Copy", false, false, {} }
				} },
				{ "View", true, false, {
					{ "Hierarchy", false, false, {} },
					{ "Inspector", false, false, {} },
					{ "Console", false, false, {} },
					{ "Reset Layout", true, false, {} }
				} },
				{ "Build", true, false, {
					{ "Build Project", false, false, {} },
					{ "Rebuild", false, false, {} },
					{ "Clean", false, false, {} },
					{ "Settings", false, false, {} }
				} },
				{ "Help", true, false, {
					{ "Documentation", false, false, {} },
					{ "Shortcuts", false, false, {} },
					{ "Report Issue", false, false, {} },
					{ "About", true, false, {} }
				} }
			};


			auto main = root.AddChild(
				fyuu_ui::SplitView{
					fyuu_ui::Orientation::Horizontal, 0.20f, 180.0f, 560.0f, 2.0f, true
				}
			);
			m_main_split_node = main.GetID();
			fyuu_ui::LayoutProperties main_layout;
			main_layout.margin = { 0.0f, 34.0f, 0.0f, 26.0f };
			main.SetLayout(main_layout);

			auto hierarchy = main.AddChild(fyuu_ui::Overlay{ true });
			hierarchy.AddChild(fyuu_ui::Border{ m_theme.surface });
			auto entities = hierarchy.AddChild(fyuu_ui::StackPanel{ fyuu_ui::Orientation::Vertical, 4.0f });
			entities.AddChild(
				fyuu_ui::TextBlock{
					"HIERARCHY",
					m_theme.muted_text,
					14.0f
				}
			);
			auto camera = entities.AddChild(fyuu_ui::ToggleButton{
				"Camera", true, true, fyuu_ui::InteractionState::Normal
			});
			auto light = entities.AddChild(fyuu_ui::ToggleButton{
				"Directional Light", false, true, fyuu_ui::InteractionState::Normal
			});
			m_camera_node = camera.GetID();
			m_light_node = light.GetID();
			m_selected_entity_node = m_camera_node;
			camera.Subscribe<fyuu_ui::ClickEvent>(
				[this](fyuu_ui::ClickEvent& event) {
					SelectEntity(m_camera_node, m_light_node);
					event.handled = true;
				}
			);
			light.Subscribe<fyuu_ui::ClickEvent>(
				[this](fyuu_ui::ClickEvent& event) {
					SelectEntity(m_light_node, m_camera_node);
					event.handled = true;
				}
			);

			auto workspace = main.AddChild(fyuu_ui::SplitView{
				fyuu_ui::Orientation::Horizontal, 0.76f, 480.0f, 240.0f, 2.0f, true
			});
			m_workspace_split_node = workspace.GetID();
			auto scene = workspace.AddChild(fyuu_ui::Overlay{ true });
			auto scene_view = scene.AddChild(
				fyuu_ui::SceneView{ m_theme.background }
			);
			m_scene_view_node = scene_view.GetID();
			auto tools = scene.AddChild(fyuu_ui::StackPanel{ fyuu_ui::Orientation::Horizontal, 6.0f });
			tools.SetLayout(FixedHeight(32.0f, fyuu_ui::Alignment::Start));
			auto translate = tools.AddChild(fyuu_ui::ToggleButton{
				"Translate", true, true, fyuu_ui::InteractionState::Normal
			});
			auto rotate = tools.AddChild(fyuu_ui::ToggleButton{
				"Rotate", false, true, fyuu_ui::InteractionState::Normal
			});
			auto scale = tools.AddChild(fyuu_ui::ToggleButton{
				"Scale", false, true, fyuu_ui::InteractionState::Normal
			});
			m_translate_node = translate.GetID();
			m_rotate_node = rotate.GetID();
			m_scale_node = scale.GetID();
			translate.Subscribe<fyuu_ui::ClickEvent>(
				[this](fyuu_ui::ClickEvent& event) {
					m_tree.GetNode(m_translate_node).GetWidget<fyuu_ui::ToggleButton>().checked = true;
					m_tree.GetNode(m_rotate_node).GetWidget<fyuu_ui::ToggleButton>().checked = false;
					m_tree.GetNode(m_scale_node).GetWidget<fyuu_ui::ToggleButton>().checked = false;
					event.handled = true;
				}
			);
			rotate.Subscribe<fyuu_ui::ClickEvent>(
				[this](fyuu_ui::ClickEvent& event) {
					m_tree.GetNode(m_translate_node).GetWidget<fyuu_ui::ToggleButton>().checked = false;
					m_tree.GetNode(m_rotate_node).GetWidget<fyuu_ui::ToggleButton>().checked = true;
					m_tree.GetNode(m_scale_node).GetWidget<fyuu_ui::ToggleButton>().checked = false;
					event.handled = true;
				}
			);
			scale.Subscribe<fyuu_ui::ClickEvent>(
				[this](fyuu_ui::ClickEvent& event) {
					m_tree.GetNode(m_translate_node).GetWidget<fyuu_ui::ToggleButton>().checked = false;
					m_tree.GetNode(m_rotate_node).GetWidget<fyuu_ui::ToggleButton>().checked = false;
					m_tree.GetNode(m_scale_node).GetWidget<fyuu_ui::ToggleButton>().checked = true;
					event.handled = true;
				}
			);
			auto zoom = tools.AddChild(fyuu_ui::Slider{
				0.25f,
				2.0f,
				1.0f,
				0.05f,
				fyuu_ui::Orientation::Horizontal,
				fyuu_ui::InteractionState::Normal
			});
			m_slider_node = zoom.GetID();

			auto inspector = workspace.AddChild(fyuu_ui::Overlay{ true });
			inspector.AddChild(fyuu_ui::Border{ m_theme.surface });
			auto properties = inspector.AddChild(fyuu_ui::StackPanel{ fyuu_ui::Orientation::Vertical, 6.0f });
			properties.AddChild(fyuu_ui::TextBlock{
				"INSPECTOR",
				m_theme.muted_text,
				14.0f
			});
			auto inspector_name = properties.AddChild(
				fyuu_ui::TextBox{ "Camera", "Name" }
			);
			m_inspector_name_node = inspector_name.GetID();
			auto inspector_visible = properties.AddChild(
				fyuu_ui::CheckBox{
					"Visible", true, true, fyuu_ui::InteractionState::Normal
				}
			);
			m_inspector_visible_node = inspector_visible.GetID();
			inspector_visible.Subscribe<fyuu_ui::ClickEvent>(
				[this](fyuu_ui::ClickEvent& event) {
					ToggleSelectedVisibility();
					event.handled = true;
				}
			);
			m_position_x_node = properties.AddChild(
				fyuu_ui::NumericBox{
					-10000.0, 10000.0, 0.0, 0.1, 2u, false, false, fyuu_ui::InteractionState::Normal
				}
			).GetID();
			m_position_y_node = properties.AddChild(
				fyuu_ui::NumericBox{
					-10000.0, 10000.0, 1.5, 0.1, 2u, false, false, fyuu_ui::InteractionState::Normal
				}
			).GetID();
			m_position_z_node = properties.AddChild(
				fyuu_ui::NumericBox{
					-10000.0, 10000.0, -5.0, 0.1, 2u, false, false, fyuu_ui::InteractionState::Normal
				}
			).GetID();

			auto status = root.AddChild(fyuu_ui::Border{ m_theme.panel });
			status.SetLayout(FixedHeight(24.0f, fyuu_ui::Alignment::End));
			auto status_text = status.AddChild(
				fyuu_ui::TextBlock{
					std::format("Ready    {}    Zoom 100%    Untitled Scene", backend_name),
					m_theme.muted_text,
					14.0f
				}
			);
			m_status_node = status_text.GetID();

			// MenuBar emits its popup panels inline, so it must sit after the main
			// content (and status bar) in the root's child order to draw on top of
			// them; window_layer above lets floating windows cover the bar row.
			auto menu_bar = root.AddChild(fyuu_ui::MenuBar{
				"Fyuu Studio",
				std::move(menu_entries)
			});
			m_menu_bar_node = menu_bar.GetID();
			menu_bar.SetLayout(FixedHeight(24.0f, fyuu_ui::Alignment::Start));
			menu_bar.Subscribe<fyuu_ui::MenuActivatedEvent>(
				[this](fyuu_ui::MenuActivatedEvent& event) {
					ActivateMenuPath(event.path);
					event.handled = true;
				}
			);

			auto window_layer = root.AddChild(fyuu_ui::WindowLayer{});
			m_window_layer_node = window_layer.GetID();

			auto close_layer = root.AddChild(fyuu_ui::Overlay{ true });
			m_close_confirmation_node = close_layer.GetID();
			close_layer.SetLayout(FixedHeight(0.0f, fyuu_ui::Alignment::Start));
			close_layer.AddChild(fyuu_ui::Border{
				{ 0.015f, 0.018f, 0.024f, 0.72f }
			});
			auto close_dialog = close_layer.AddChild(fyuu_ui::Overlay{ true });
			m_close_dialog_node = close_dialog.GetID();
			auto close_dialog_layout = FixedHeight(156.0f, fyuu_ui::Alignment::Center);
			close_dialog_layout.width = 380.0f;
			close_dialog.SetLayout(close_dialog_layout);
			close_dialog.AddChild(fyuu_ui::Border{
				m_theme.raised_surface
			});
			auto close_content = close_dialog.AddChild(
				fyuu_ui::StackPanel{ fyuu_ui::Orientation::Vertical, 10.0f }
			);
			fyuu_ui::LayoutProperties close_content_layout;
			close_content_layout.margin = { 20.0f, 18.0f, 20.0f, 18.0f };
			close_content.SetLayout(close_content_layout);
			close_content.AddChild(fyuu_ui::TextBlock{
				"Save changes before closing?",
				m_theme.text,
				16.0f
			});
			close_content.AddChild(fyuu_ui::TextBlock{
				"Unsaved changes will be lost.",
				m_theme.muted_text,
				14.0f
			});
			auto close_actions = close_content.AddChild(
				fyuu_ui::StackPanel{ fyuu_ui::Orientation::Horizontal, 8.0f }
			);
			auto save = close_actions.AddChild(fyuu_ui::Button{
				"Save", true, true, fyuu_ui::InteractionState::Normal
			});
			auto discard = close_actions.AddChild(fyuu_ui::Button{
				"Discard", true, false, fyuu_ui::InteractionState::Normal
			});
			auto cancel = close_actions.AddChild(fyuu_ui::Button{
				"Cancel", true, false, fyuu_ui::InteractionState::Normal
			});
			m_close_save_node = save.GetID();
			m_close_discard_node = discard.GetID();
			m_close_cancel_node = cancel.GetID();
			save.Subscribe<fyuu_ui::ClickEvent>(
				[this](fyuu_ui::ClickEvent& event) {
					m_close_confirmation_open = false;
					auto layout = FixedHeight(0.0f, fyuu_ui::Alignment::Start);
					m_tree.GetNode(m_close_confirmation_node).SetLayout(layout);
					m_commands.emplace_back(StudioCommand::SaveAndClose);
					event.handled = true;
				}
			);
			discard.Subscribe<fyuu_ui::ClickEvent>(
				[this](fyuu_ui::ClickEvent& event) {
					m_close_confirmation_open = false;
					auto layout = FixedHeight(0.0f, fyuu_ui::Alignment::Start);
					m_tree.GetNode(m_close_confirmation_node).SetLayout(layout);
					m_commands.emplace_back(StudioCommand::DiscardAndClose);
					event.handled = true;
				}
			);
			cancel.Subscribe<fyuu_ui::ClickEvent>(
				[this](fyuu_ui::ClickEvent& event) {
					m_close_confirmation_open = false;
					auto layout = FixedHeight(0.0f, fyuu_ui::Alignment::Start);
					m_tree.GetNode(m_close_confirmation_node).SetLayout(layout);
					event.handled = true;
				}
			);
		}

	public:
		StudioUI(std::uint32_t width, std::uint32_t height, std::string_view backend_name)
			: m_theme(fyuu_ui::DarkTheme()),
			m_tree(fyuu_ui::Overlay{}),
			m_backend_name(backend_name),
			m_logical_width(width),
			m_logical_height(height),
			m_pixel_width(width),
			m_pixel_height(height) {
			BuildEditorShell(backend_name);
		}

		void ResetDocument() {
			m_camera_visible = true;
			m_light_visible = true;
			m_tree.GetNode(m_camera_node).GetWidget<fyuu_ui::ToggleButton>().title = "Camera";
			m_tree.GetNode(m_light_node).GetWidget<fyuu_ui::ToggleButton>().title = "Directional Light";
			SelectEntity(m_camera_node, m_light_node);
			m_tree.GetNode(m_position_x_node).GetWidget<fyuu_ui::NumericBox>().value = 0.0;
			m_tree.GetNode(m_position_y_node).GetWidget<fyuu_ui::NumericBox>().value = 1.5;
			m_tree.GetNode(m_position_z_node).GetWidget<fyuu_ui::NumericBox>().value = -5.0;
			m_status_context = "New Scene";
			UpdateZoomStatus();
		}

		void DrainCommands(std::vector<StudioCommand>& output) {
			output.clear();
			output.swap(m_commands);
		}

		void SetDocumentState(
			std::string_view title,
			std::uint64_t revision,
			bool dirty
		) {
			m_document_title = title;
			m_document_revision = revision;
			m_document_dirty = dirty;
			UpdateZoomStatus();
		}

		void ShowCloseConfirmation() {
			m_close_confirmation_open = true;
			CloseMenu();
			fyuu_ui::LayoutProperties layout;
			m_tree.GetNode(m_close_confirmation_node).SetLayout(layout);
		}

		void ProcessEvent(fyuu_desktop::Event const& event) override {
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
			if (m_close_confirmation_open &&
				event.type == fyuu_desktop::EventType::KeyPressed &&
				event.key == fyuu_desktop::Key::Escape) {
				m_close_confirmation_open = false;
				auto layout = FixedHeight(0.0f, fyuu_ui::Alignment::Start);
				m_tree.GetNode(m_close_confirmation_node).SetLayout(layout);
				return;
			}
			if (m_close_confirmation_open &&
				event.type == fyuu_desktop::EventType::KeyPressed &&
				event.key == fyuu_desktop::Key::Enter) {
				m_close_confirmation_open = false;
				auto layout = FixedHeight(0.0f, fyuu_ui::Alignment::Start);
				m_tree.GetNode(m_close_confirmation_node).SetLayout(layout);
				m_commands.emplace_back(StudioCommand::SaveAndClose);
				return;
			}
			if (event.type == fyuu_desktop::EventType::MouseMoved) {
				m_pointer_x = event.x;
				m_pointer_y = event.y;
				UpdatePointerState(event.x, event.y);
				if (m_window_drag_node.has_value()) {
					UpdateWindowPosition(event.x, event.y);
				}
				else if (m_window_resize_node.has_value()) {
					UpdateWindowSize(event.x, event.y);
				}
				else if (m_split_drag_node.has_value()) {
					UpdateSplit(event.x);
				}
				else if (m_numeric_drag_node.has_value()) {
					UpdateNumericBox(event.x);
				}
				else if (m_drag_bounds.has_value()) {
					UpdateSlider(event.x, event.y);
				}
				return;
			}
			if (event.type == fyuu_desktop::EventType::MouseWheel) {
				auto tree = m_tree.BuildVisualTree(
					{ static_cast<float>(m_logical_width), static_cast<float>(m_logical_height) },
					m_theme
				);
				auto const hit = tree.HitTest({ m_pointer_x, m_pointer_y });
				if (hit.has_value() && hit->logical_id == m_scene_view_node) {
					AdjustSlider(event.delta_y);
				}
				else if (hit.has_value() && IsNumericNode(hit->logical_id)) {
					AdjustNumericBox(
						hit->logical_id,
						static_cast<double>(event.delta_y),
						event.shift,
						event.control
					);
				}
				return;
			}
			if (event.type == fyuu_desktop::EventType::KeyPressed &&
				m_focused_node.has_value() &&
				IsNumericNode(*m_focused_node) &&
				(event.key == fyuu_desktop::Key::LeftArrow ||
					event.key == fyuu_desktop::Key::DownArrow)) {
				AdjustNumericBox(*m_focused_node, -1.0, event.shift, event.control);
				return;
			}
			if (event.type == fyuu_desktop::EventType::KeyPressed &&
				m_focused_node.has_value() &&
				IsNumericNode(*m_focused_node) &&
				(event.key == fyuu_desktop::Key::RightArrow ||
					event.key == fyuu_desktop::Key::UpArrow)) {
				AdjustNumericBox(*m_focused_node, 1.0, event.shift, event.control);
				return;
			}
			if (event.type == fyuu_desktop::EventType::KeyPressed &&
				m_focused_node.has_value() &&
				IsNumericNode(*m_focused_node) &&
				event.key == fyuu_desktop::Key::Home) {
				auto& numeric = m_tree.GetNode(*m_focused_node).GetWidget<fyuu_ui::NumericBox>();
				numeric.value = numeric.minimum;
				m_commands.emplace_back(StudioCommand::DocumentEdited);
				return;
			}
			if (event.type == fyuu_desktop::EventType::KeyPressed &&
				m_focused_node.has_value() &&
				IsNumericNode(*m_focused_node) &&
				event.key == fyuu_desktop::Key::End) {
				auto& numeric = m_tree.GetNode(*m_focused_node).GetWidget<fyuu_ui::NumericBox>();
				numeric.value = numeric.maximum;
				m_commands.emplace_back(StudioCommand::DocumentEdited);
				return;
			}
			if (event.type == fyuu_desktop::EventType::KeyPressed &&
				m_focused_node == m_slider_node &&
				(event.key == fyuu_desktop::Key::LeftArrow ||
					event.key == fyuu_desktop::Key::DownArrow)) {
				AdjustSlider(-1.0f);
				return;
			}
			if (event.type == fyuu_desktop::EventType::KeyPressed &&
				m_focused_node == m_slider_node &&
				(event.key == fyuu_desktop::Key::RightArrow ||
					event.key == fyuu_desktop::Key::UpArrow)) {
				AdjustSlider(1.0f);
				return;
			}
			if (event.type == fyuu_desktop::EventType::TextInput &&
				m_focused_node == m_inspector_name_node) {
				InsertText(event.text);
				return;
			}
			if (event.type == fyuu_desktop::EventType::KeyPressed &&
				m_focused_node == m_inspector_name_node &&
				event.key == fyuu_desktop::Key::Backspace) {
				ErasePreviousCodePoint();
				return;
			}
			if (event.type == fyuu_desktop::EventType::KeyPressed &&
				m_focused_node == m_inspector_name_node &&
				event.key == fyuu_desktop::Key::Delete) {
				EraseNextCodePoint();
				return;
			}
			if (event.type == fyuu_desktop::EventType::KeyPressed &&
				m_focused_node == m_inspector_name_node &&
				event.key == fyuu_desktop::Key::LeftArrow) {
				auto& text_box = m_tree.GetNode(m_inspector_name_node).GetWidget<fyuu_ui::TextBox>();
				text_box.caret_offset = PreviousCodePoint(text_box.text, text_box.caret_offset);
				return;
			}
			if (event.type == fyuu_desktop::EventType::KeyPressed &&
				m_focused_node == m_inspector_name_node &&
				event.key == fyuu_desktop::Key::RightArrow) {
				auto& text_box = m_tree.GetNode(m_inspector_name_node).GetWidget<fyuu_ui::TextBox>();
				text_box.caret_offset = NextCodePoint(text_box.text, text_box.caret_offset);
				return;
			}
			if (event.type == fyuu_desktop::EventType::KeyPressed &&
				m_focused_node == m_inspector_name_node &&
				event.key == fyuu_desktop::Key::Home) {
				m_tree.GetNode(m_inspector_name_node).GetWidget<fyuu_ui::TextBox>().caret_offset = 0u;
				return;
			}
			if (event.type == fyuu_desktop::EventType::KeyPressed &&
				m_focused_node == m_inspector_name_node &&
				event.key == fyuu_desktop::Key::End) {
				auto& text_box = m_tree.GetNode(m_inspector_name_node).GetWidget<fyuu_ui::TextBox>();
				text_box.caret_offset = text_box.text.size();
				return;
			}
			if (event.type == fyuu_desktop::EventType::KeyPressed &&
				m_focused_node == m_inspector_name_node &&
				event.key == fyuu_desktop::Key::Enter) {
				EndNameEdit(true);
				return;
			}
			if (event.type == fyuu_desktop::EventType::KeyPressed &&
				m_focused_node == m_inspector_name_node &&
				event.key == fyuu_desktop::Key::Escape) {
				EndNameEdit(false);
				return;
			}
			if ((event.type == fyuu_desktop::EventType::MouseButtonPressed ||
				event.type == fyuu_desktop::EventType::MouseButtonReleased) &&
				event.mouse_button != fyuu_desktop::MouseButton::Left) {
				return;
			}
			auto visual_tree = m_tree.BuildVisualTree(
				{ static_cast<float>(m_logical_width), static_cast<float>(m_logical_height) },
				m_theme
			);
			if (event.type == fyuu_desktop::EventType::MouseButtonPressed) {
				m_pointer_x = event.x;
				m_pointer_y = event.y;
				if (!m_close_confirmation_open) {
					FocusWindow(event.x, event.y);
				}
				auto const hit = visual_tree.HitTest({ event.x, event.y });
				if (!hit.has_value()) {
					m_pressed_node.reset();
					ClearPointerStates();
					CloseMenu();
					EndNameEdit(true);
					return;
				}
				if (!m_close_confirmation_open &&
					BeginWindowInteraction(*hit)) {
					CloseMenu();
					return;
				}
				if (m_close_confirmation_open &&
					hit->logical_id != m_close_save_node &&
					hit->logical_id != m_close_discard_node &&
					hit->logical_id != m_close_cancel_node) {
					m_pressed_node.reset();
					return;
				}
				if (m_menu_bar_node != 0u && hit->logical_id != m_menu_bar_node) {
					CloseMenu();
				}
				m_pressed_node = hit->logical_id;
				ClearPointerStates();
				SetPointerState(hit->logical_id, fyuu_ui::InteractionState::Pressed);
				if (m_menu_bar_node != 0u && hit->logical_id == m_menu_bar_node) {
					if (hit->menu_path.has_value()) {
						m_pressed_menu_path = hit->menu_path;
						m_tree.GetNode(m_menu_bar_node).GetWidget<fyuu_ui::MenuBar>().pressed_path =
							hit->menu_path->indices;
					}
					else {
						CloseMenu();
					}
				}
				if (hit->logical_id == m_inspector_name_node) {
					if (m_focused_node.has_value() && IsNumericNode(*m_focused_node)) {
						m_tree.GetNode(*m_focused_node).GetWidget<fyuu_ui::NumericBox>().focused = false;
						m_focused_node.reset();
					}
					BeginNameEdit();
				}
				else {
					EndNameEdit(true);
					if (m_focused_node.has_value() && IsNumericNode(*m_focused_node)) {
						m_tree.GetNode(*m_focused_node).GetWidget<fyuu_ui::NumericBox>().focused = false;
						m_focused_node.reset();
					}
				}
				if (hit->logical_id == m_slider_node) {
					m_focused_node = m_slider_node;
					m_drag_bounds = fyuu_ui::Rect{ { event.x - hit->position.x, event.y - hit->position.y }, hit->size };
					UpdateSlider(event.x, event.y);
				}
				else if (hit->logical_id == m_main_split_node ||
					hit->logical_id == m_workspace_split_node) {
					auto& split = m_tree.GetNode(hit->logical_id).GetContainer<fyuu_ui::SplitView>();
					m_split_drag_node = hit->logical_id;
					m_split_drag_start = event.x;
					m_split_drag_value = split.split;
					if (hit->logical_id == m_main_split_node) {
						m_split_drag_extent = (std::max)(
							1.0f,
							static_cast<float>(m_logical_width) - split.spacing
						);
					}
					else {
						auto const& main_split = m_tree.GetNode(m_main_split_node).GetContainer<fyuu_ui::SplitView>();
						m_split_drag_extent = (std::max)(
							1.0f,
							static_cast<float>(m_logical_width) * (1.0f - main_split.split) -
								split.spacing
						);
					}
				}
				else if (IsNumericNode(hit->logical_id)) {
					auto& numeric = m_tree.GetNode(hit->logical_id).GetWidget<fyuu_ui::NumericBox>();
					if (!numeric.read_only) {
						if (m_focused_node.has_value() && IsNumericNode(*m_focused_node)) {
							m_tree.GetNode(*m_focused_node).GetWidget<fyuu_ui::NumericBox>().focused = false;
							m_focused_node.reset();
						}
						m_tree.GetNode(hit->logical_id).GetWidget<fyuu_ui::NumericBox>().focused = true;
						m_focused_node = hit->logical_id;
						m_numeric_drag_node = hit->logical_id;
						m_numeric_drag_start = event.x;
						m_numeric_drag_value = numeric.value;
					}
				}
				return;
			}
			if (event.type != fyuu_desktop::EventType::MouseButtonReleased) {
				return;
			}
			if (m_window_drag_node.has_value()) {
				m_window_drag_node.reset();
				m_pressed_node.reset();
				UpdatePointerState(event.x, event.y);
				return;
			}
			if (m_window_resize_node.has_value()) {
				m_window_resize_node.reset();
				m_window_resize_region = fyuu_ui::WindowResizeRegion::None;
				m_pressed_node.reset();
				UpdatePointerState(event.x, event.y);
				return;
			}
			if (m_drag_bounds.has_value()) {
				UpdateSlider(event.x, event.y);
				m_drag_bounds.reset();
			}
			m_split_drag_node.reset();
			m_numeric_drag_node.reset();
			auto const released = visual_tree.HitTest({ event.x, event.y });
			if (m_pressed_menu_path.has_value()) {
				auto& bar = m_tree.GetNode(m_menu_bar_node).GetWidget<fyuu_ui::MenuBar>();
				auto const press = *m_pressed_menu_path;
				m_pressed_menu_path.reset();
				bar.pressed_path.reset();
				auto const on_same_entry = released.has_value() &&
					released->logical_id == m_menu_bar_node &&
					released->menu_path.has_value() &&
					released->menu_path->indices == press.indices;
				if (on_same_entry) {
					auto const* entry = fyuu_ui::FindMenuEntry(bar, press.indices);
					if (entry != nullptr && !entry->children.empty()) {
						// Submenu header: toggle its dropdown / cascade.
						if (bar.open_path == press.indices) {
							bar.open_path.reset();
						}
						else {
							bar.open_path = press.indices;
						}
					}
					else {
						fyuu_ui::MenuActivatedEvent event{ press };
						m_tree.GetNode(m_menu_bar_node).Dispatch(event);
					}
				}
				else if (released.has_value() && released->logical_id == m_menu_bar_node &&
					released->menu_path.has_value()) {
					auto const& path = released->menu_path->indices;
					if (path.size() == 1u) {
						// Drag released on another bar item: switch the open menu.
						bar.open_path = path;
					}
					else {
						auto const* entry = fyuu_ui::FindMenuEntry(bar, path);
						if (entry != nullptr && !entry->children.empty()) {
							bar.open_path = path;
						}
					}
				}
			}
			else if (released.has_value() && released->logical_id == m_pressed_node) {
				auto node = m_tree.GetNode(released->logical_id);
				fyuu_ui::ClickEvent click{ { event.x, event.y } };
				node.Dispatch(click);
			}
			m_pressed_node.reset();
			UpdatePointerState(event.x, event.y);
		}

		std::vector<fyuu_ui::DrawCommand> BuildDrawList() const {
			auto tree = m_tree.BuildVisualTree(
				{ static_cast<float>(m_logical_width), static_cast<float>(m_logical_height) },
				m_theme
			);
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

}
