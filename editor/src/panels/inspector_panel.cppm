module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <format>
#include <numbers>
#include <string>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#include <imgui.h>

module fyuu_editor:inspector_panel;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_engine;
import :context;

namespace {
	using fyuu_editor::EditorContext;

	// Inspector call chain: EditorApplication::DrawDefaultLayout -> DrawInspectorPanel
	// -> helper/ImGui events -> EditorContext transaction -> EditorScene mutation.
	// Called by DrawInspectorPanel for entity/component identifiers. Converts through
	// the UUID facade and optionally forwards the text to ImGui's clipboard.
	void DrawUUID(char const* label, fyuu_asset::UUID const& id) {
		ImGui::TextUnformatted(label);
		ImGui::SameLine();
		if (id.IsNil()) {
			ImGui::TextDisabled("None");
			return;
		}
		auto const text = id.ToString();
		ImGui::TextUnformatted(text.c_str());
		ImGui::SameLine();
		ImGui::PushID(label);
		if (ImGui::SmallButton("Copy")) {
			ImGui::SetClipboardText(text.c_str());
		}
		ImGui::PopID();
	}

	// Called after mesh/material controls are drawn. Reads a 16-byte Asset Browser
	// payload, reconstructs UUID through the facade, and reports acceptance to caller.
	bool AcceptAsset(
		char const* payload_type,
		fyuu_asset::UUID& output
	) {
		if (!ImGui::BeginDragDropTarget()) {
			return false;
		}
		auto accepted = false;
		if (auto const* payload = ImGui::AcceptDragDropPayload(payload_type)) {
			if (payload->DataSize == 16) {
				std::uint8_t bytes[16];
				auto const* source = static_cast<std::uint8_t const*>(payload->Data);
				std::ranges::copy_n(source, 16u, bytes);
				output = fyuu_asset::UUID::FromBytes(bytes);
				accepted = true;
			}
		}
		ImGui::EndDragDropTarget();
		return accepted;
	}

	// Called by DrawInspectorPanel before rendering rotation. Converts stored quaternion
	// radians to user-facing Euler degrees; it never mutates the scene.
	std::array<float, 3> QuaternionToEuler(std::array<float, 4> const& rotation) {
		auto const x = rotation[0];
		auto const y = rotation[1];
		auto const z = rotation[2];
		auto const w = rotation[3];
		auto const roll = std::atan2(
			2.0f * (w * x + y * z),
			1.0f - 2.0f * (x * x + y * y)
			);
		auto const pitch = std::asin(std::clamp(2.0f * (w * y - z * x), -1.0f, 1.0f));
		auto const yaw = std::atan2(
			2.0f * (w * z + x * y),
			1.0f - 2.0f * (y * y + z * z)
			);
		constexpr auto radians_to_degrees = 180.0f / std::numbers::pi_v<float>;
		return {
			roll * radians_to_degrees,
			pitch * radians_to_degrees,
			yaw * radians_to_degrees
		};
	}

	// Called when the rotation widget changes. Converts displayed Euler degrees back
	// to the quaternion representation persisted by the scene asset.
	std::array<float, 4> EulerToQuaternion(std::array<float, 3> const& rotation) {
		constexpr auto degrees_to_radians = std::numbers::pi_v<float> / 180.0f;
		auto const half_x = rotation[0] * degrees_to_radians * 0.5f;
		auto const half_y = rotation[1] * degrees_to_radians * 0.5f;
		auto const half_z = rotation[2] * degrees_to_radians * 0.5f;
		auto const sin_x = std::sin(half_x);
		auto const cos_x = std::cos(half_x);
		auto const sin_y = std::sin(half_y);
		auto const cos_y = std::cos(half_y);
		auto const sin_z = std::sin(half_z);
		auto const cos_z = std::cos(half_z);
		return {
			sin_x * cos_y * cos_z - cos_x * sin_y * sin_z,
			cos_x * sin_y * cos_z + sin_x * cos_y * sin_z,
			cos_x * cos_y * sin_z - sin_x * sin_y * cos_z,
			cos_x * cos_y * cos_z + sin_x * sin_y * sin_z
		};
	}

}

namespace fyuu_editor {

	// Called once per frame by DrawDefaultLayout. Finds the selected entity, renders all
	// editable fields, wraps each gesture in BeginEdit/CommitEdit, and marks mutations
	// dirty. Asset selection calls engine discovery or accepts Asset Browser payloads.
	void DrawInspectorPanel(EditorContext& context, fyuu_engine::AssetStore* store) {
		enum class AssetTarget {
			None,
			Mesh,
			Material
		};
		static std::vector<fyuu_engine::AssetEntry> assets;
		static AssetTarget asset_target = AssetTarget::None;

		bool delete_selected = false;
		if (ImGui::Begin("Inspector")) {
			// Selection is resolved every frame because scene-vector mutation can invalidate
			// pointers retained from an earlier frame.
			if (auto* entity = context.scene.FindEntity(context.selected_entity)) {
				DrawUUID("ID", entity->id);
				ImGui::Separator();
				std::array<char, 128> name{};
				auto const formatted_name = std::format("{}", entity->name);
				auto const name_size = std::min(formatted_name.size(), name.size() - 1u);
				std::ranges::copy_n(formatted_name.begin(), name_size, name.begin());
				name[name_size] = '\0';
				auto const name_changed = ImGui::InputText("Name", name.data(), name.size());
				if (ImGui::IsItemActivated()) {
					context.BeginEdit();
				}
				if (name_changed) {
					entity->name = name.data();
					context.scene.MarkDirty();
				}
				if (ImGui::IsItemDeactivatedAfterEdit()) {
					context.CommitEdit();
				}
				if (entity->name.empty()) {
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.3f, 1.0f));
					ImGui::TextUnformatted("Name cannot be empty");
					ImGui::PopStyleColor();
				}
				ImGui::SeparatorText("Transform");
				if (ImGui::Button("Reset Transform")) {
					context.BeginEdit();
					entity->translation = { 0.0f, 0.0f, 0.0f };
					entity->rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
					entity->scale = { 1.0f, 1.0f, 1.0f };
					context.scene.MarkDirty();
					context.CommitEdit();
					context.Log("Reset entity transform");
				}
				bool changed = false;
				// Called below for Position and Scale. It turns ImGui activation/deactivation
				// into one EditorContext transaction while reporting frame-level value changes.
				// Called for Position and Scale. Converts widget activation/deactivation into one
				// EditorContext transaction and reports frame-level mutations through changed.
				auto EditVector = [&context, &changed](
					char const* label,
					float* value,
					float speed,
					float minimum
				) {
					if (ImGui::DragFloat3(label, value, speed, minimum)) {
						changed = true;
					}
					if (ImGui::IsItemActivated()) {
						context.BeginEdit();
					}
					if (ImGui::IsItemDeactivatedAfterEdit()) {
						context.CommitEdit();
					}
					};
				EditVector("Position", entity->translation.data(), 0.1f, 0.0f);
				auto euler_rotation = QuaternionToEuler(entity->rotation);
				if (ImGui::DragFloat3("Rotation", euler_rotation.data(), 0.25f)) {
					entity->rotation = EulerToQuaternion(euler_rotation);
					changed = true;
				}
				if (ImGui::IsItemActivated()) {
					context.BeginEdit();
				}
				if (ImGui::IsItemDeactivatedAfterEdit()) {
					context.CommitEdit();
				}
				EditVector("Scale", entity->scale.data(), 0.05f, 0.001f);
				if (changed) {
					context.scene.MarkDirty();
				}
				if (!entity->parent.IsNil()) {
					ImGui::SeparatorText("Hierarchy");
					if (auto const* parent = context.scene.FindEntity(entity->parent)) {
						auto const parent_name = std::format("Parent: {}", parent->name);
						ImGui::TextUnformatted(parent_name.c_str());
					}
					DrawUUID("Parent ID", entity->parent);
					if (ImGui::Button("Move to Root")) {
						context.BeginEdit();
						context.scene.SetParent(entity->id, {});
						context.CommitEdit();
						context.Log("Moved entity to scene root");
					}
				}
				ImGui::SeparatorText("Components");
				DrawUUID("Mesh", entity->mesh);
				if (ImGui::Button("Select Mesh") && store) {
					try {
						assets = fyuu_engine::DiscoverAssets(*store, "Mesh");
						asset_target = AssetTarget::Mesh;
						ImGui::OpenPopup("Select Asset");
					}
					catch (std::exception const& exception) {
						context.Log(std::format("Failed to enumerate meshes: {}", exception.what()));
					}
				}
				fyuu_asset::UUID dropped_mesh{};
				if (AcceptAsset("FYUU_MESH_ASSET", dropped_mesh)) {
					context.BeginEdit();
					entity->mesh = dropped_mesh;
					context.scene.MarkDirty();
					context.CommitEdit();
					context.Log("Changed mesh reference");
				}
				if (!entity->mesh.IsNil()) {
					ImGui::SameLine();
					if (ImGui::Button("Clear Mesh")) {
						context.BeginEdit();
						entity->mesh = {};
						context.scene.MarkDirty();
						context.CommitEdit();
						context.Log("Cleared mesh reference");
					}
				}
				DrawUUID("Material", entity->material);
				if (ImGui::Button("Select Material") && store) {
					try {
						assets = fyuu_engine::DiscoverAssets(*store, "Material");
						asset_target = AssetTarget::Material;
						ImGui::OpenPopup("Select Asset");
					}
					catch (std::exception const& exception) {
						context.Log(std::format("Failed to enumerate materials: {}", exception.what()));
					}
				}
				fyuu_asset::UUID dropped_material{};
				if (AcceptAsset("FYUU_MATERIAL_ASSET", dropped_material)) {
					context.BeginEdit();
					entity->material = dropped_material;
					context.scene.MarkDirty();
					context.CommitEdit();
					context.Log("Changed material reference");
				}
				if (!entity->material.IsNil()) {
					ImGui::SameLine();
					if (ImGui::Button("Clear Material")) {
						context.BeginEdit();
						entity->material = {};
						context.scene.MarkDirty();
						context.CommitEdit();
						context.Log("Cleared material reference");
					}
				}
				// The shared modal consumes the category cached by either Select button, then
				// applies the chosen UUID through the same transaction path as drag/drop.
				if (ImGui::BeginPopupModal(
					"Select Asset",
					nullptr,
					ImGuiWindowFlags_AlwaysAutoResize)) {
					if (assets.empty()) {
						ImGui::TextDisabled("No assets found");
					}
					for (auto const& asset : assets) {
						if (ImGui::Selectable(asset.name.c_str())) {
							context.BeginEdit();
							if (asset_target == AssetTarget::Mesh) {
								entity->mesh = asset.id;
							}
							else if (asset_target == AssetTarget::Material) {
								entity->material = asset.id;
							}
							context.scene.MarkDirty();
							context.CommitEdit();
							context.Log("Changed asset reference");
							asset_target = AssetTarget::None;
							ImGui::CloseCurrentPopup();
						}
					}
					if (ImGui::Button("Cancel")) {
						asset_target = AssetTarget::None;
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}
				ImGui::Spacing();
				delete_selected = ImGui::Button("Delete Entity");
			}
			else {
				ImGui::TextDisabled("No entity selected");
			}
		}
		ImGui::End();

		// Deletion is delayed until after ImGui::End so removing a vector element cannot
		// invalidate the entity pointer while the inspector still renders it.
		if (delete_selected) {
			context.BeginEdit();
			auto const selected = context.selected_entity;
			if (context.scene.DestroyEntity(selected)) {
				context.selected_entity = {};
				context.CommitEdit();
				context.Log("Deleted entity");
			}
			else {
				context.CommitEdit();
				context.Log("Cannot delete an entity that has children");
			}
		}
	}

}
