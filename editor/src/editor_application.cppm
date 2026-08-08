module;
#include <imgui.h>
#include "fyuu_application.h"
#include "fyuu_log.h"

module fyuu_editor:application;
import :context;
import :console_panel;
import :hierarchy_panel;
import :inspector_panel;
import :scene_panel;

namespace fyuu_editor {

	class EditorApplication {
	public:
		int Run(int argc, char** argv) {
			Fyuu_App application{
				.description = "FyuuEngine scene editor",
				.name = "FyuuEditor",
				.title = "Fyuu Editor",
				.surface_width = 1440u,
				.surface_height = 900u,
				.version = {0u, 1u, 0u, 0u},
				.font_size = 16.0f,
				.user_data = this,
				.Init = InitializeCallback,
				.Tick = TickCallback,
				.Shutdown = ShutdownCallback,
			};
			return Fyuu_Run(argc, argv, &application);
		}

	private:
		static void InitializeCallback(Fyuu_App* application) NOEXCEPT {
			static_cast<EditorApplication*>(application->user_data)->Initialize();
		}

		static void TickCallback(Fyuu_App* application) NOEXCEPT {
			static_cast<EditorApplication*>(application->user_data)->Tick();
		}

		static void ShutdownCallback(Fyuu_App* application) NOEXCEPT {
			static_cast<EditorApplication*>(application->user_data)->Shutdown();
		}

		void Initialize() {
			m_context.scene.CreateEntity("Camera");
			m_context.selected_entity = m_context.scene.CreateEntity("Entity");
			m_context.scene.MarkSaved();
			m_context.Log("Fyuu Editor initialized");
			FYUU_LOG_INFO("Fyuu Editor initialized");
		}

    void Tick() {
		ProcessShortcuts();
        DrawMainMenu();
			DrawDefaultLayout();
			if (m_context.show_demo_window) {
				ImGui::ShowDemoWindow(&m_context.show_demo_window);
			}
		}

		void Shutdown() {
			FYUU_LOG_INFO("Fyuu Editor shutdown");
		}

		void DrawMainMenu() {
			if (!ImGui::BeginMainMenuBar()) {
				return;
			}
			if (ImGui::BeginMenu("File")) {
				ImGui::MenuItem("New Scene", "Ctrl+N", false, false);
				ImGui::MenuItem("Open Scene...", "Ctrl+O", false, false);
				ImGui::MenuItem("Save Scene", "Ctrl+S", false, false);
				ImGui::EndMenu();
			}
        if (ImGui::BeginMenu("Entity")) {
            if (ImGui::MenuItem("Create Empty")) {
				m_context.BeginEdit();
                m_context.selected_entity = m_context.scene.CreateEntity("Entity");
				m_context.CommitEdit();
                m_context.Log("Created entity");
            }
            ImGui::EndMenu();
        }
		if (ImGui::BeginMenu("Edit")) {
			if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_context.CanUndo())) {
				m_context.Undo();
			}
			if (ImGui::MenuItem("Redo", "Ctrl+Y", false, m_context.CanRedo())) {
				m_context.Redo();
			}
			ImGui::EndMenu();
		}
			if (ImGui::BeginMenu("View")) {
				ImGui::MenuItem("ImGui Demo", nullptr, &m_context.show_demo_window);
				ImGui::EndMenu();
			}
        ImGui::EndMainMenuBar();
    }

	void ProcessShortcuts() {
		auto const& io = ImGui::GetIO();
		if (!io.KeyCtrl || io.WantTextInput) {
			return;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
			m_context.Undo();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
			m_context.Redo();
		}
	}

		void DrawDefaultLayout() {
			auto const display = ImGui::GetIO().DisplaySize;
			constexpr float menu_height = 22.0f;
			constexpr float console_height = 210.0f;
			constexpr float side_width = 280.0f;
			auto const content_height = display.y - menu_height - console_height;

			ImGui::SetNextWindowPos(ImVec2(0.0f, menu_height), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(side_width, content_height), ImGuiCond_Always);
			DrawHierarchyPanel(m_context);

			ImGui::SetNextWindowPos(ImVec2(side_width, menu_height), ImGuiCond_Always);
			ImGui::SetNextWindowSize(
				ImVec2(display.x - side_width * 2.0f, content_height), ImGuiCond_Always);
			DrawScenePanel();

			ImGui::SetNextWindowPos(
				ImVec2(display.x - side_width, menu_height), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(side_width, content_height), ImGuiCond_Always);
			DrawInspectorPanel(m_context);

			ImGui::SetNextWindowPos(
				ImVec2(0.0f, display.y - console_height), ImGuiCond_Always);
			ImGui::SetNextWindowSize(ImVec2(display.x, console_height), ImGuiCond_Always);
			DrawConsolePanel(m_context);
		}

		EditorContext m_context;
	};

	int RunApplication(int argc, char** argv) {
		EditorApplication editor;
		return editor.Run(argc, argv);
	}

}
