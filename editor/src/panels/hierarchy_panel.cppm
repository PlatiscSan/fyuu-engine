module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#include <imgui.h>

module fyuu_editor:hierarchy_panel;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_engine;
import :context;

namespace {
	using fyuu_editor::EditorContext;
	using fyuu_editor::Entity;
	using fyuu_editor::EntityID;

	// Hierarchy call chain: EditorApplication::DrawDefaultLayout -> DrawHierarchyPanel
	// -> SubtreeMatches/DrawEntityNode -> EditorContext transaction -> EditorScene.
	constexpr char EntityPayload[] = "FYUU_SCENE_ENTITY";
	constexpr char MeshPayload[] = "FYUU_MESH_ASSET";
	constexpr char MaterialPayload[] = "FYUU_MATERIAL_ASSET";

	// Called by DrawEntityNode for entity/mesh/material drops. Validates the ABI-neutral
	// 16-byte payload and delegates reconstruction to the UUID facade.
	bool ReadUUID(ImGuiPayload const* payload, EntityID& output) {
		if (!payload || payload->DataSize != 16) {
			return false;
		}
		std::uint8_t bytes[16];
		auto const* source = static_cast<std::uint8_t const*>(payload->Data);
		std::ranges::copy_n(source, 16u, bytes);
		output = fyuu_asset::UUID::FromBytes(bytes);
		return true;
	}

	// Called by SubtreeMatches; tests display name and stable UUID string without
	// changing selection or document state.
	bool MatchesFilter(Entity const& entity, std::string_view filter) {
		return filter.empty()
		|| entity.name.find(filter) != std::string::npos
		|| entity.id.ToString().find(filter) != std::string::npos;
	}

	// Called by DrawHierarchyPanel. Uses an explicit stack to scan descendants so
	// parents remain visible when only a child matches.
	bool SubtreeMatches(
		EditorContext const& context,
		Entity const& entity,
		std::string_view filter
	) {
		std::vector<EntityID> pending{ entity.id };
		for (std::size_t visited = 0u; !pending.empty(); ++visited) {
			if (visited >= context.scene.Entities().size()) {
				return false;
			}
			auto const current = pending.back();
			pending.pop_back();
			for (auto const& candidate : context.scene.Entities()) {
				if (std::is_eq(candidate.id <=> current)
					&& MatchesFilter(candidate, filter)) {
					return true;
				}
				if (std::is_eq(candidate.parent <=> current)) {
					pending.push_back(candidate.id);
				}
			}
		}
		return false;
	}

	// Called iteratively from DrawHierarchyPanel. Renders one entity row and
	// translates drag/drop gestures into BeginEdit -> model mutation -> CommitEdit.
	void DrawEntityNode(
		EditorContext& context,
		Entity const& entity,
		std::size_t depth
	) {
		ImGui::Indent(static_cast<float>(depth) * 16.0f);
		std::uint8_t id[16];
		entity.id.ToBytes(id);
		auto const* id_begin = reinterpret_cast<char const*>(id);
		ImGui::PushID(id_begin, id_begin + 16);

		auto flags = ImGuiTreeNodeFlags_OpenOnArrow
		| ImGuiTreeNodeFlags_SpanAvailWidth;
		flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (std::is_eq(context.selected_entity <=> entity.id)) {
			flags |= ImGuiTreeNodeFlags_Selected;
		}
		ImGui::TreeNodeEx(entity.name.c_str(), flags);
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
			context.selected_entity = entity.id;
		}

		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload(EntityPayload, id, sizeof(id));
			ImGui::TextUnformatted(entity.name.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget()) {
			EntityID dropped{};
			if (ReadUUID(ImGui::AcceptDragDropPayload(EntityPayload), dropped)) {
				context.BeginEdit();
				if (context.scene.SetParent(dropped, entity.id)) {
					context.Log("Changed entity parent");
				}
				else {
					context.Log("Cannot create a cyclic entity hierarchy");
				}
				context.CommitEdit();
			}
			if (ReadUUID(ImGui::AcceptDragDropPayload(MeshPayload), dropped)) {
				context.BeginEdit();
				if (auto* target = context.scene.FindEntity(entity.id)) {
					target->mesh = dropped;
					context.scene.MarkDirty();
					context.Log("Changed mesh reference");
				}
				context.CommitEdit();
			}
			if (ReadUUID(ImGui::AcceptDragDropPayload(MaterialPayload), dropped)) {
				context.BeginEdit();
				if (auto* target = context.scene.FindEntity(entity.id)) {
					target->material = dropped;
					context.scene.MarkDirty();
					context.Log("Changed material reference");
				}
				context.CommitEdit();
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::PopID();
		ImGui::Unindent(static_cast<float>(depth) * 16.0f);
	}

}

namespace fyuu_editor {

	// Called once per frame by DrawDefaultLayout. Handles root creation/filtering, then
	// calls SubtreeMatches and DrawEntityNode for every visible root hierarchy.
	void DrawHierarchyPanel(EditorContext& context) {
		static std::array<char, 128> filter{};
		if (ImGui::Begin("Hierarchy")) {
			if (ImGui::Button("+ Create Entity")) {
				context.BeginEdit();
				context.selected_entity = context.scene.CreateEntity("Entity");
				context.CommitEdit();
				context.Log("Created entity");
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputTextWithHint("##HierarchyFilter", "Search entities", filter.data(), filter.size());
			ImGui::Separator();
			auto const filter_text = std::string_view{ filter.data() };
			for (auto const& entity : context.scene.Entities()) {
				if (!SubtreeMatches(context, entity, filter_text)) {
					continue;
				}
				auto depth = std::size_t{ 0u };
				auto parent = entity.parent;
				while (!parent.IsNil()
					&& depth < context.scene.Entities().size()) {
					auto const ancestor = std::ranges::find_if(
						context.scene.Entities(),
						[&parent](Entity const& candidate) {
							return std::is_eq(candidate.id <=> parent);
						}
					);
					if (ancestor == context.scene.Entities().end()) {
						break;
					}
					parent = ancestor->parent;
					++depth;
				}
				DrawEntityNode(context, entity, depth);
			}
		}
		ImGui::End();
	}

}
