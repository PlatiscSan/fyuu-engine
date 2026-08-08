module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <filesystem>
#include <memory>
#endif // !defined(__cpp_lib_modules)
#include <imgui.h>
#include "fyuu_application.h"
#include "fyuu_log.h"

module fyuu_editor:application;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_engine;
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
			auto& editor = *static_cast<EditorApplication*>(application->user_data);
			try {
				editor.Initialize();
			}
			catch (std::exception const& exception) {
				editor.m_context.Log(exception.what());
				FYUU_LOG_ERROR(exception.what());
			}
			catch (...) {
				editor.m_context.Log("Unknown editor initialization error");
				FYUU_LOG_ERROR("Unknown editor initialization error");
			}
		}

		static void TickCallback(Fyuu_App* application) NOEXCEPT {
			static_cast<EditorApplication*>(application->user_data)->Tick();
		}

		static void ShutdownCallback(Fyuu_App* application) NOEXCEPT {
			static_cast<EditorApplication*>(application->user_data)->Shutdown();
		}

		void Initialize() {
			m_asset_store = std::make_unique<fyuu_engine::AssetStore>(
				std::filesystem::current_path() / "assets"
			);
			m_context.scene.CreateEntity("Camera");
			m_context.selected_entity = m_context.scene.CreateEntity("Entity");
			m_context.scene.MarkSaved();
			m_context.Log("Fyuu Editor initialized");
			FYUU_LOG_INFO("Fyuu Editor initialized");
		}

    void Tick() {
		UpdateSave();
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
				if (ImGui::MenuItem(
					"New Scene",
					"Ctrl+N",
					false,
					!m_context.scene.Dirty() && !m_context.scene.Saving())) {
					NewScene();
				}
				ImGui::MenuItem("Open Scene...", "Ctrl+O", false, false);
				if (ImGui::MenuItem(
					"Save Scene",
					"Ctrl+S",
					false,
					m_asset_store && !m_context.scene.Saving())) {
					SaveScene();
				}
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
		else if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
			SaveScene();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_N, false)
			&& !m_context.scene.Dirty()) {
			NewScene();
		}
	}

	void SaveScene() {
		if (!m_asset_store) {
			m_context.Log("Cannot save scene without an asset store");
			return;
		}
		if (m_context.scene.BeginSave()) {
			m_context.Log("Saving scene...");
		}
		else if (m_context.scene.Saving()) {
			m_context.Log("Scene save is already in progress");
		}
		else {
			m_context.Log("Cannot save an invalid scene");
		}
	}

	void UpdateSave() {
		switch (m_context.scene.UpdateSave()) {
			case EditorScene::SaveResult::Succeeded:
				m_context.Log("Scene saved");
				break;
			case EditorScene::SaveResult::Failed:
				m_context.Log("Scene save failed: " + m_context.scene.SaveError());
				break;
			case EditorScene::SaveResult::None:
				break;
		}
	}

	void NewScene() {
		if (m_context.NewScene()) {
			m_context.Log("Created new scene");
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

		std::unique_ptr<fyuu_engine::AssetStore> m_asset_store;
		EditorContext m_context;
	};

	int RunApplication(int argc, char** argv) {
		EditorApplication editor;
		return editor.Run(argc, argv);
	}

}
