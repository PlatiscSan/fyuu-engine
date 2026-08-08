module;
#include <imgui.h>

module fyuu_editor:scene_panel;

namespace fyuu_editor {

	void DrawScenePanel() {
		if (ImGui::Begin("Scene")) {
			auto const available = ImGui::GetContentRegionAvail();
			auto const origin = ImGui::GetCursorScreenPos();
			auto* draw_list = ImGui::GetWindowDrawList();
			draw_list->AddRectFilled(
				origin,
				ImVec2(origin.x + available.x, origin.y + available.y),
				IM_COL32(25, 28, 34, 255));
			auto const text = ImGui::CalcTextSize("Scene viewport will render here");
			draw_list->AddText(
				ImVec2(origin.x + (available.x - text.x) * 0.5f,
					origin.y + (available.y - text.y) * 0.5f),
				IM_COL32(145, 150, 160, 255),
				"Scene viewport will render here");
			ImGui::Dummy(available);
		}
		ImGui::End();
	}

}
