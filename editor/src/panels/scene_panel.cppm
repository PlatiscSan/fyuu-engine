module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#endif // !defined(__cpp_lib_modules)
#include <imgui.h>

module fyuu_editor:scene_panel;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_engine;
import :context;

namespace fyuu_editor {

	// Called once per frame by EditorApplication::DrawDefaultLayout. Implements a
	// top-down X/Z viewport and routes a full drag through BeginEdit -> MarkDirty ->
	// CommitEdit so the gesture produces one undo record.
	void DrawScenePanel(EditorContext& context) {
		// Function-static values are transient panel state. They survive frames but are
		// never serialized and never participate in document snapshots.
		static ImVec2 view_offset{ 0.0f, 0.0f };
		static float view_scale = 48.0f;
		static EntityID dragged_entity;
		static bool dragging_entity = false;
		static bool snap_to_grid = false;
		static float snap_step = 0.5f;
		static ImVec2 drag_origin;
		static std::array<float, 3> drag_translation{};

		if (ImGui::Begin("Scene")) {
			// Phase 1: toolbar commands mutate only view/interaction state.
			auto const frame_selected_requested = ImGui::Button("Frame Selected");
			ImGui::SameLine();
			auto const frame_all_requested = ImGui::Button("Frame All");
			ImGui::SameLine();
			if (ImGui::Button("Reset View")) {
				view_offset = {};
				view_scale = 48.0f;
			}
			ImGui::SameLine();
			ImGui::Checkbox("Snap", &snap_to_grid);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(120.0f);
			ImGui::DragFloat("Step", &snap_step, 0.05f, 0.05f, 10.0f);
			auto const available = ImGui::GetContentRegionAvail();
			auto const origin = ImGui::GetCursorScreenPos();
			auto const center = ImVec2{
				origin.x + available.x * 0.5f,
				origin.y + available.y * 0.5f
			};
			auto* draw_list = ImGui::GetWindowDrawList();
			draw_list->AddRectFilled(
				origin,
				ImVec2(origin.x + available.x, origin.y + available.y),
				IM_COL32(25, 28, 34, 255));

			// Phase 2: capture canvas input, frame entities, and handle pan/zoom.
			ImGui::InvisibleButton(
				"SceneCanvas",
				available,
				ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle
				);
			auto const hovered = ImGui::IsItemHovered();
			auto const& io = ImGui::GetIO();
			if ((frame_selected_requested
				|| (hovered && ImGui::IsKeyPressed(ImGuiKey_F, false)))
				&& !context.selected_entity.IsNil()) {
				if (auto const* selected = context.scene.FindEntity(context.selected_entity)) {
					view_offset = {
						-selected->translation[0] * view_scale,
						selected->translation[2] * view_scale
					};
				}
			}
			if (frame_all_requested
				|| (hovered && ImGui::IsKeyPressed(ImGuiKey_Home, false))) {
				auto const& entities = context.scene.Entities();
				if (!entities.empty()) {
					auto minimum_x = entities.front().translation[0];
					auto maximum_x = minimum_x;
					auto minimum_z = entities.front().translation[2];
					auto maximum_z = minimum_z;
					for (auto const& entity : entities) {
						minimum_x = std::min(minimum_x, entity.translation[0]);
						maximum_x = std::max(maximum_x, entity.translation[0]);
						minimum_z = std::min(minimum_z, entity.translation[2]);
						maximum_z = std::max(maximum_z, entity.translation[2]);
					}
					auto const extent_x = std::max(maximum_x - minimum_x, 1.0f);
					auto const extent_z = std::max(maximum_z - minimum_z, 1.0f);
					view_scale = std::clamp(
						std::min(
						available.x * 0.8f / extent_x,
						available.y * 0.8f / extent_z
						),
						8.0f,
						256.0f
						);
					view_offset = {
						-(minimum_x + maximum_x) * 0.5f * view_scale,
						(minimum_z + maximum_z) * 0.5f * view_scale
					};
				}
			}
			if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
				view_offset.x += io.MouseDelta.x;
				view_offset.y += io.MouseDelta.y;
			}
			if (hovered && io.MouseWheel != 0.0f) {
				auto const old_scale = view_scale;
				view_scale = std::clamp(
					view_scale * std::pow(1.15f, io.MouseWheel),
					8.0f,
					256.0f
					);
				auto const mouse_from_center = ImVec2{
					io.MousePos.x - center.x,
					io.MousePos.y - center.y
				};
				view_offset.x = mouse_from_center.x
				- (mouse_from_center.x - view_offset.x) * view_scale / old_scale;
				view_offset.y = mouse_from_center.y
				- (mouse_from_center.y - view_offset.y) * view_scale / old_scale;
			}

			// Phase 3: draw the adaptive grid and world axes inside the canvas clip.
			draw_list->PushClipRect(
				origin,
				ImVec2(origin.x + available.x, origin.y + available.y),
				true
				);
			auto grid_spacing = view_scale;
			while (grid_spacing < 24.0f) {
				grid_spacing *= 2.0f;
			}
			auto const grid_origin = ImVec2{
				center.x + view_offset.x,
				center.y + view_offset.y
			};
			for (auto x = std::fmod(grid_origin.x - origin.x, grid_spacing);
				x < available.x;
				x += grid_spacing) {
				draw_list->AddLine(
					ImVec2(origin.x + x, origin.y),
					ImVec2(origin.x + x, origin.y + available.y),
					IM_COL32(50, 54, 63, 255)
					);
			}
			for (auto y = std::fmod(grid_origin.y - origin.y, grid_spacing);
				y < available.y;
				y += grid_spacing) {
				draw_list->AddLine(
					ImVec2(origin.x, origin.y + y),
					ImVec2(origin.x + available.x, origin.y + y),
					IM_COL32(50, 54, 63, 255)
					);
			}
			draw_list->AddLine(
				ImVec2(grid_origin.x, origin.y),
				ImVec2(grid_origin.x, origin.y + available.y),
				IM_COL32(160, 70, 70, 255),
				2.0f
				);
			draw_list->AddLine(
				ImVec2(origin.x, grid_origin.y),
				ImVec2(origin.x + available.x, grid_origin.y),
				IM_COL32(70, 110, 170, 255),
				2.0f
				);

			// Phase 4: draw/hit-test entities using UUIDs; no entity pointer survives a frame.
			EntityID hovered_entity;
			auto nearest_distance = std::numeric_limits<float>::max();
			for (auto const& entity : context.scene.Entities()) {
				auto const position = ImVec2{
					grid_origin.x + entity.translation[0] * view_scale,
					grid_origin.y - entity.translation[2] * view_scale
				};
				auto const selected = std::is_eq(context.selected_entity <=> entity.id);
				draw_list->AddCircleFilled(
					position,
					selected ? 7.0f : 5.0f,
					selected
					? IM_COL32(255, 184, 75, 255)
					: IM_COL32(125, 180, 235, 255)
					);
				draw_list->AddText(
					ImVec2(position.x + 9.0f, position.y - ImGui::GetTextLineHeight() * 0.5f),
					IM_COL32(210, 214, 222, 255),
					entity.name.c_str()
					);

				auto const delta_x = io.MousePos.x - position.x;
				auto const delta_y = io.MousePos.y - position.y;
				auto const distance = delta_x * delta_x + delta_y * delta_y;
				if (hovered && distance < 144.0f && distance < nearest_distance) {
					hovered_entity = entity.id;
					nearest_distance = distance;
				}
			}
			// Phase 5: mouse-down begins the transaction, drag frames mutate X/Z, and
			// mouse-up commits the complete gesture to history.
			if (hovered
				&& ImGui::IsMouseClicked(ImGuiMouseButton_Left)
				&& !hovered_entity.IsNil()) {
				context.selected_entity = hovered_entity;
				dragged_entity = hovered_entity;
				dragging_entity = true;
				drag_origin = io.MousePos;
				if (auto const* entity = context.scene.FindEntity(dragged_entity)) {
					drag_translation = entity->translation;
				}
				context.BeginEdit();
			}
			if (dragging_entity && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
				if (auto* entity = context.scene.FindEntity(dragged_entity)) {
					auto x = drag_translation[0] + (io.MousePos.x - drag_origin.x) / view_scale;
					auto z = drag_translation[2] - (io.MousePos.y - drag_origin.y) / view_scale;
					if (snap_to_grid) {
						x = std::round(x / snap_step) * snap_step;
						z = std::round(z / snap_step) * snap_step;
					}
					if (entity->translation[0] != x || entity->translation[2] != z) {
						entity->translation[0] = x;
						entity->translation[2] = z;
						context.scene.MarkDirty();
					}
				}
				else {
					dragging_entity = false;
					context.CommitEdit();
				}
			}
			if (dragging_entity && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
				dragging_entity = false;
				dragged_entity = {};
				context.CommitEdit();
			}
			draw_list->AddText(
				ImVec2(origin.x + 10.0f, origin.y + 10.0f),
				IM_COL32(145, 150, 160, 255),
				"LMB Drag: Move X/Z  |  MMB: Pan  |  Wheel: Zoom  |  F: Selected  |  Home: All"
				);
			draw_list->PopClipRect();
		}
		ImGui::End();
	}

}
