module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <array>
#include <format>
#include <string>
#include <string_view>
#endif // !defined(__cpp_lib_modules)
#include <imgui.h>

module fyuu_editor:console_panel;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :context;

namespace fyuu_editor {

	// Called once per frame by EditorApplication::DrawDefaultLayout. Consumes messages
	// appended by EditorContext::Log and maintains only filter/scroll presentation state.
	void DrawConsolePanel(EditorContext& context) {
		static std::array<char, 128> filter{};
		static bool auto_scroll = true;
		static std::size_t previous_message_count = 0u;

		if (ImGui::Begin("Console")) {
			if (ImGui::Button("Clear")) {
				context.console_messages.clear();
			}
			ImGui::SameLine();
			if (ImGui::Button("Copy All")) {
				std::string text;
				for (auto const& message : context.console_messages) {
					text.append(message);
					text.push_back('\n');
				}
				ImGui::SetClipboardText(text.c_str());
			}
			ImGui::SameLine();
			ImGui::Checkbox("Auto-scroll", &auto_scroll);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputTextWithHint("##ConsoleFilter", "Filter messages", filter.data(), filter.size());

			auto const count_text = std::format("{} messages", context.console_messages.size());
			ImGui::TextUnformatted(count_text.c_str());
			ImGui::Separator();
			if (ImGui::BeginChild("ConsoleMessages")) {
				auto const was_at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f;
				auto const filter_text = std::string_view{ filter.data() };
				for (auto const& message : context.console_messages) {
					if (filter_text.empty() || message.find(filter_text) != std::string::npos) {
						ImGui::TextUnformatted(message.c_str());
					}
				}
				if (auto_scroll
					&& was_at_bottom
					&& previous_message_count != context.console_messages.size()) {
					ImGui::SetScrollHereY(1.0f);
				}
			}
			ImGui::EndChild();
			previous_message_count = context.console_messages.size();
		}
		ImGui::End();
	}

}
