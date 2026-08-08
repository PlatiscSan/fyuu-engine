module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdio>
#include <array>
#endif // !defined(__cpp_lib_modules)
#include <imgui.h>

module fyuu_editor:inspector_panel;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :context;

namespace fyuu_editor {

	void DrawInspectorPanel(EditorContext& context) {
		bool delete_selected = false;
		if (ImGui::Begin("Inspector")) {
			if (auto* entity = context.scene.FindEntity(context.selected_entity)) {
            std::array<char, 128> name{};
            std::snprintf(name.data(), name.size(), "%s", entity->name.c_str());
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
            ImGui::SeparatorText("Transform");
            bool changed = false;
			auto EditVector = [&context, &changed](char const* label, float* value, float speed, float minimum) {
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
			if (ImGui::DragFloat4("Rotation", entity->rotation.data(), 0.01f, -1.0f, 1.0f)) {
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
				ImGui::Spacing();
				delete_selected = ImGui::Button("Delete Entity");
			}
			else {
				ImGui::TextDisabled("No entity selected");
			}
		}
		ImGui::End();

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
		}
    }
}

}
