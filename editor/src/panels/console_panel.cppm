module;
#include <imgui.h>

module fyuu_editor:console_panel;
import :context;

namespace fyuu_editor {

	void DrawConsolePanel(EditorContext& context) {
		if (ImGui::Begin("Console")) {
			if (ImGui::Button("Clear")) {
				context.console_messages.clear();
			}
			ImGui::Separator();
			for (auto const& message : context.console_messages) {
				ImGui::TextUnformatted(message.c_str());
			}
			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
				ImGui::SetScrollHereY(1.0f);
			}
		}
		ImGui::End();
	}

}
