module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#endif // !defined(__cpp_lib_modules)
#include <imgui.h>

module fyuu_editor:hierarchy_panel;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_engine;
import :context;

namespace fyuu_editor {

	void DrawHierarchyPanel(EditorContext& context) {
		if (ImGui::Begin("Hierarchy")) {
        if (ImGui::Button("+ Create Entity")) {
			context.BeginEdit();
            context.selected_entity = context.scene.CreateEntity("Entity");
			context.CommitEdit();
            context.Log("Created entity");
			}
			ImGui::Separator();
			for (auto const& entity : context.scene.Entities()) {
				std::uint8_t id[16];
				fyuu_asset::UUIDToBytes(entity.id, id);
				auto const* id_begin = reinterpret_cast<char const*>(id);
				ImGui::PushID(id_begin, id_begin + 16);
				if (ImGui::Selectable(
						entity.name.c_str(),
						fyuu_asset::UUIDEqual(context.selected_entity, entity.id))) {
					context.selected_entity = entity.id;
				}
				ImGui::PopID();
			}
		}
		ImGui::End();
	}

}
