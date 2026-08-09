module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#include <imgui.h>

module fyuu_editor:scene_panel;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_engine;
import :context;

namespace fyuu_editor {

	enum class GizmoAxis {
		None,
		X,
		Z,
		Rotate
	};

	enum class GizmoMode {
		Translate,
		Rotate,
		Scale
	};

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
		static GizmoAxis dragged_axis = GizmoAxis::None;
		static GizmoMode gizmo_mode = GizmoMode::Translate;
		static GizmoMode dragged_mode = GizmoMode::Translate;
		static std::array<float, 4> drag_world_rotation{};
		static std::array<float, 3> drag_scale{};
		static float drag_angle = 0.0f;

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
			ImGui::SameLine();
			if (ImGui::Button("Translate (W)")) {
				gizmo_mode = GizmoMode::Translate;
			}
			ImGui::SameLine();
			if (ImGui::Button("Rotate (E)")) {
				gizmo_mode = GizmoMode::Rotate;
			}
			ImGui::SameLine();
			if (ImGui::Button("Scale (R)")) {
				gizmo_mode = GizmoMode::Scale;
			}
			auto const available = ImGui::GetContentRegionAvail();
			auto const origin = ImGui::GetCursorScreenPos();
			auto const center = ImVec2{
				origin.x + available.x * 0.5f,
				origin.y + available.y * 0.5f
			};
			fyuu_engine::SceneViewSubmission submission;
			submission.camera_position = {
				-view_offset.x / view_scale,
				view_offset.y / view_scale
			};
			submission.pixels_per_unit = view_scale;
			submission.entities.reserve(context.scene.Entities().size());
			auto const& entities = context.scene.Entities();
			std::vector<std::int64_t> parent_indices;
			parent_indices.reserve(entities.size());
			for (std::size_t entity_index = 0u; entity_index < entities.size(); ++entity_index) {
				auto const& entity = entities[entity_index];
				auto parent_index = std::int64_t{ -1 };
				for (std::size_t candidate_index = 0u; candidate_index < entities.size(); ++candidate_index) {
					if (std::is_eq(entities[candidate_index].id <=> entity.parent)) {
						parent_index = static_cast<std::int64_t>(candidate_index);
						break;
					}
				}
				parent_indices.push_back(parent_index);
				submission.entities.push_back({
					.translation = entity.translation,
					.rotation = entity.rotation,
					.scale = entity.scale,
					.parent_index = parent_index,
					.selected = std::is_eq(context.selected_entity <=> entity.id)
				});
			}
			fyuu_engine::ResolveSceneView(submission);
			fyuu_engine::SubmitSceneView(submission);
			auto* draw_list = ImGui::GetWindowDrawList();
			// Texture ID 2 is reserved by RenderingSystem for the RHI scene target.
			// Image emits the textured ImGui draw command; RenderingSystem discovers
			// it after ImGui::Render and schedules the offscreen pass before the GUI pass.
			ImGui::Image(2u, available);
			ImGui::SetCursorScreenPos(origin);

			// Phase 2: capture canvas input, frame entities, and handle pan/zoom.
			ImGui::InvisibleButton(
				"SceneCanvas",
				available,
				ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle
				);
			auto const hovered = ImGui::IsItemHovered();
			auto const& io = ImGui::GetIO();
			if (hovered && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
				gizmo_mode = GizmoMode::Translate;
			}
			if (hovered && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
				gizmo_mode = GizmoMode::Rotate;
			}
			if (hovered && ImGui::IsKeyPressed(ImGuiKey_R, false)) {
				gizmo_mode = GizmoMode::Scale;
			}
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

			// Phase 3: clip editor overlays to the RHI viewport. The adaptive grid and
			// world axes are generated by RenderingSystem from the submitted camera.
			draw_list->PushClipRect(
				origin,
				ImVec2(origin.x + available.x, origin.y + available.y),
				true
				);
			auto const grid_origin = ImVec2{
				center.x + view_offset.x,
				center.y + view_offset.y
			};

			// Phase 4: the RHI pass draws entity markers; the editor overlays labels and
			// performs hit testing using UUIDs. No entity pointer survives a frame.
			EntityID hovered_entity;
			auto nearest_distance = std::numeric_limits<float>::max();
			auto selected_index = entities.size();
			for (std::size_t entity_index = 0u; entity_index < entities.size(); ++entity_index) {
				auto const& entity = entities[entity_index];
				auto const& world_entity = submission.entities[entity_index];
				auto const position = ImVec2{
					grid_origin.x + world_entity.translation[0] * view_scale,
					grid_origin.y - world_entity.translation[2] * view_scale
				};
				if (std::is_eq(context.selected_entity <=> entity.id)) {
					selected_index = entity_index;
				}
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
			auto hovered_axis = GizmoAxis::None;
			if (selected_index < entities.size()) {
				auto const& selected_world = submission.entities[selected_index];
				auto const gizmo_origin = ImVec2{
					grid_origin.x + selected_world.translation[0] * view_scale,
					grid_origin.y - selected_world.translation[2] * view_scale
				};
				if (gizmo_mode == GizmoMode::Rotate) {
					draw_list->AddCircle(gizmo_origin, 48.0f, IM_COL32(235, 190, 70, 255), 0, 3.0f);
					auto const delta_x = io.MousePos.x - gizmo_origin.x;
					auto const delta_y = io.MousePos.y - gizmo_origin.y;
					auto const distance = std::sqrt(delta_x * delta_x + delta_y * delta_y);
					if (hovered && std::abs(distance - 48.0f) <= 7.0f) {
						hovered_axis = GizmoAxis::Rotate;
					}
				}
				else if (gizmo_mode == GizmoMode::Scale) {
					auto const x_direction = fyuu_engine::SceneViewDirection(
						selected_world,
						{ 1.0f, 0.0f, 0.0f }
					);
					auto const z_direction = fyuu_engine::SceneViewDirection(
						selected_world,
						{ 0.0f, 0.0f, 1.0f }
					);
					auto const x_end = ImVec2{
						gizmo_origin.x + x_direction[0] * 56.0f,
						gizmo_origin.y - x_direction[2] * 56.0f
					};
					auto const z_end = ImVec2{
						gizmo_origin.x + z_direction[0] * 56.0f,
						gizmo_origin.y - z_direction[2] * 56.0f
					};
					draw_list->AddLine(gizmo_origin, x_end, IM_COL32(220, 70, 70, 255), 3.0f);
					draw_list->AddLine(gizmo_origin, z_end, IM_COL32(70, 125, 220, 255), 3.0f);
					draw_list->AddRectFilled(
						ImVec2{ x_end.x - 5.0f, x_end.y - 5.0f },
						ImVec2{ x_end.x + 5.0f, x_end.y + 5.0f },
						IM_COL32(220, 70, 70, 255)
					);
					draw_list->AddRectFilled(
						ImVec2{ z_end.x - 5.0f, z_end.y - 5.0f },
						ImVec2{ z_end.x + 5.0f, z_end.y + 5.0f },
						IM_COL32(70, 125, 220, 255)
					);
					auto const x_delta = ImVec2{ io.MousePos.x - x_end.x, io.MousePos.y - x_end.y };
					auto const z_delta = ImVec2{ io.MousePos.x - z_end.x, io.MousePos.y - z_end.y };
					if (hovered && x_delta.x * x_delta.x + x_delta.y * x_delta.y <= 100.0f) {
						hovered_axis = GizmoAxis::X;
					}
					if (hovered && z_delta.x * z_delta.x + z_delta.y * z_delta.y <= 100.0f) {
						hovered_axis = GizmoAxis::Z;
					}
				}
				else {
					auto const x_end = ImVec2{ gizmo_origin.x + 56.0f, gizmo_origin.y };
					auto const z_end = ImVec2{ gizmo_origin.x, gizmo_origin.y - 56.0f };
					draw_list->AddLine(gizmo_origin, x_end, IM_COL32(220, 70, 70, 255), 3.0f);
					draw_list->AddTriangleFilled(
						ImVec2{ x_end.x + 7.0f, x_end.y },
						ImVec2{ x_end.x - 2.0f, x_end.y - 5.0f },
						ImVec2{ x_end.x - 2.0f, x_end.y + 5.0f },
						IM_COL32(220, 70, 70, 255)
					);
					draw_list->AddLine(gizmo_origin, z_end, IM_COL32(70, 125, 220, 255), 3.0f);
					draw_list->AddTriangleFilled(
						ImVec2{ z_end.x, z_end.y - 7.0f },
						ImVec2{ z_end.x - 5.0f, z_end.y + 2.0f },
						ImVec2{ z_end.x + 5.0f, z_end.y + 2.0f },
						IM_COL32(70, 125, 220, 255)
					);
					if (hovered
						&& io.MousePos.x >= gizmo_origin.x
						&& io.MousePos.x <= x_end.x + 7.0f
						&& std::abs(io.MousePos.y - gizmo_origin.y) <= 7.0f) {
						hovered_axis = GizmoAxis::X;
					}
					if (hovered
						&& io.MousePos.y <= gizmo_origin.y
						&& io.MousePos.y >= z_end.y - 7.0f
						&& std::abs(io.MousePos.x - gizmo_origin.x) <= 7.0f) {
						hovered_axis = GizmoAxis::Z;
					}
				}
			}
			// Phase 5: mouse-down begins the transaction, drag frames mutate X/Z, and
			// mouse-up commits the complete gesture to history.
			if (hovered
				&& ImGui::IsMouseClicked(ImGuiMouseButton_Left)
				&& (hovered_axis != GizmoAxis::None || !hovered_entity.IsNil())) {
				if (hovered_axis != GizmoAxis::None) {
					dragged_entity = context.selected_entity;
					dragged_axis = hovered_axis;
				}
				else {
					context.selected_entity = hovered_entity;
					dragged_entity = hovered_entity;
					dragged_axis = GizmoAxis::None;
				}
				dragging_entity = true;
				dragged_mode = gizmo_mode;
				drag_origin = io.MousePos;
				if (auto const* entity = context.scene.FindEntity(dragged_entity)) {
					drag_translation = entity->translation;
					drag_scale = entity->scale;
				}
				if (dragged_axis == GizmoAxis::Rotate && selected_index < submission.entities.size()) {
					auto const& selected_world = submission.entities[selected_index];
					drag_world_rotation = selected_world.rotation;
					auto const gizmo_x = grid_origin.x + selected_world.translation[0] * view_scale;
					auto const gizmo_y = grid_origin.y - selected_world.translation[2] * view_scale;
					drag_angle = std::atan2(io.MousePos.x - gizmo_x, -(io.MousePos.y - gizmo_y));
				}
				context.BeginEdit();
			}
			if (dragging_entity && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
				if (auto* entity = context.scene.FindEntity(dragged_entity)) {
					if (dragged_axis == GizmoAxis::Rotate) {
						for (std::size_t entity_index = 0u; entity_index < entities.size(); ++entity_index) {
							if (!std::is_eq(entities[entity_index].id <=> dragged_entity)) {
								continue;
							}
							auto const& world_entity = submission.entities[entity_index];
							auto const gizmo_x = grid_origin.x + world_entity.translation[0] * view_scale;
							auto const gizmo_y = grid_origin.y - world_entity.translation[2] * view_scale;
							auto const current_angle = std::atan2(
								io.MousePos.x - gizmo_x,
								-(io.MousePos.y - gizmo_y)
							);
							auto rotation = fyuu_engine::SceneViewRotateY(
								drag_world_rotation,
								current_angle - drag_angle
							);
							auto const parent_index = parent_indices[entity_index];
							if (parent_index >= 0
								&& static_cast<std::size_t>(parent_index) < submission.entities.size()) {
								rotation = fyuu_engine::SceneViewLocalRotation(
									submission.entities[static_cast<std::size_t>(parent_index)],
									rotation
								);
							}
							if (entity->rotation != rotation) {
								entity->rotation = rotation;
								context.scene.MarkDirty();
							}
							break;
						}
					}
					if (dragged_mode == GizmoMode::Scale
						&& (dragged_axis == GizmoAxis::X || dragged_axis == GizmoAxis::Z)) {
						for (std::size_t entity_index = 0u; entity_index < entities.size(); ++entity_index) {
							if (!std::is_eq(entities[entity_index].id <=> dragged_entity)) {
								continue;
							}
							auto const local_direction = dragged_axis == GizmoAxis::X
								? std::array{ 1.0f, 0.0f, 0.0f }
								: std::array{ 0.0f, 0.0f, 1.0f };
							auto const world_direction = fyuu_engine::SceneViewDirection(
								submission.entities[entity_index],
								local_direction
							);
							auto const screen_x = world_direction[0];
							auto const screen_y = -world_direction[2];
							auto const length = std::max(
								std::sqrt(screen_x * screen_x + screen_y * screen_y),
								0.000001f
							);
							auto const amount = (
								(io.MousePos.x - drag_origin.x) * screen_x
								+ (io.MousePos.y - drag_origin.y) * screen_y
							) / (length * 56.0f);
							auto scale = drag_scale;
							auto const component = dragged_axis == GizmoAxis::X ? 0u : 2u;
							scale[component] = std::max(
								drag_scale[component] * (1.0f + amount),
								0.001f
							);
							if (entity->scale != scale) {
								entity->scale = scale;
								context.scene.MarkDirty();
							}
							break;
						}
					}
					auto const scaling = dragged_mode == GizmoMode::Scale
						&& (dragged_axis == GizmoAxis::X || dragged_axis == GizmoAxis::Z);
					auto world_delta = (dragged_axis == GizmoAxis::Rotate || scaling)
						? std::array{ 0.0f, 0.0f, 0.0f }
						: std::array{
							(io.MousePos.x - drag_origin.x) / view_scale,
							0.0f,
							-(io.MousePos.y - drag_origin.y) / view_scale
						};
					if (dragged_axis == GizmoAxis::X) {
						world_delta[2] = 0.0f;
					}
					if (dragged_axis == GizmoAxis::Z) {
						world_delta[0] = 0.0f;
					}
					for (std::size_t entity_index = 0u; entity_index < entities.size(); ++entity_index) {
						if (!std::is_eq(entities[entity_index].id <=> dragged_entity)) {
							continue;
						}
						auto const parent_index = parent_indices[entity_index];
						if (parent_index >= 0
							&& static_cast<std::size_t>(parent_index) < submission.entities.size()) {
							world_delta = fyuu_engine::SceneViewLocalVector(
								submission.entities[static_cast<std::size_t>(parent_index)],
								world_delta
							);
						}
						break;
					}
					auto x = drag_translation[0] + world_delta[0];
					auto z = drag_translation[2] + world_delta[2];
					if (snap_to_grid) {
						x = std::round(x / snap_step) * snap_step;
						z = std::round(z / snap_step) * snap_step;
					}
					if (!scaling
						&& dragged_axis != GizmoAxis::Rotate
						&& (entity->translation[0] != x || entity->translation[2] != z)) {
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
				dragged_axis = GizmoAxis::None;
				context.CommitEdit();
			}
			draw_list->AddText(
				ImVec2(origin.x + 10.0f, origin.y + 10.0f),
				IM_COL32(145, 150, 160, 255),
				"W: Translate  |  E: Rotate  |  R: Scale  |  MMB: Pan  |  Wheel: Zoom  |  F: Selected"
				);
			draw_list->PopClipRect();
		}
		ImGui::End();
	}

}
