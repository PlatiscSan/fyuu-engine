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
		DrawDocumentPopups();
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
					!m_context.scene.Saving())) {
					RequestNewScene();
				}
				if (ImGui::MenuItem(
					"Open Scene...",
					"Ctrl+O",
					false,
					m_asset_store && !m_context.scene.Saving())) {
					OpenSceneBrowser();
				}
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
		else if (ImGui::IsKeyPressed(ImGuiKey_N, false)) {
			RequestNewScene();
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
			OpenSceneBrowser();
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
				if (m_execute_document_action_after_save && !m_context.scene.Dirty()) {
					m_execute_document_action_after_save = false;
					ExecutePendingDocumentAction();
				}
				else if (m_execute_document_action_after_save) {
					m_execute_document_action_after_save = false;
					m_open_save_confirmation = true;
				}
				break;
			case EditorScene::SaveResult::Failed:
				m_context.Log("Scene save failed: " + m_context.scene.SaveError());
				m_execute_document_action_after_save = false;
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

	void OpenSceneBrowser() {
		if (!m_asset_store || m_context.scene.Saving()) {
			return;
		}
		try {
			m_scene_assets = fyuu_engine::DiscoverScenes(*m_asset_store);
			m_open_scene_browser = true;
		}
		catch (std::exception const& exception) {
			m_context.Log("Failed to enumerate scenes: " + std::string{ exception.what() });
		}
	}

	void RequestNewScene() {
		if (m_context.scene.Saving()) {
			return;
		}
		if (!m_context.scene.Dirty()) {
			NewScene();
			return;
		}
		m_pending_document_action = PendingDocumentAction::New;
		m_open_save_confirmation = true;
	}

	void RequestOpenScene(fyuu_asset::UUID const& id) {
		m_pending_scene = id;
		if (!m_context.scene.Dirty()) {
			OpenPendingScene();
			return;
		}
		m_pending_document_action = PendingDocumentAction::Open;
		m_open_save_confirmation = true;
	}

	void ExecutePendingDocumentAction() {
		auto const action = m_pending_document_action;
		m_pending_document_action = PendingDocumentAction::None;
		if (action == PendingDocumentAction::New) {
			NewScene();
		}
		else if (action == PendingDocumentAction::Open) {
			OpenPendingScene();
		}
	}

	void OpenPendingScene() {
		if (!m_asset_store) {
			return;
		}
		try {
			auto asset = fyuu_engine::LoadScene(*m_asset_store, m_pending_scene);
			if (m_context.LoadScene(std::move(asset))) {
				m_context.Log("Opened scene " + fyuu_asset::UUIDToString(m_pending_scene));
			}
		}
		catch (std::exception const& exception) {
			m_context.Log("Failed to open scene: " + std::string{ exception.what() });
		}
		m_pending_document_action = PendingDocumentAction::None;
	}

	void DrawDocumentPopups() {
		if (m_open_scene_browser) {
			ImGui::OpenPopup("Open Scene");
			m_open_scene_browser = false;
		}
		if (ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (m_scene_assets.empty()) {
				ImGui::TextDisabled("No scene assets found");
			}
			for (auto const& scene : m_scene_assets) {
				if (ImGui::Selectable(scene.name.c_str())) {
					RequestOpenScene(scene.id);
					ImGui::CloseCurrentPopup();
				}
			}
			if (ImGui::Button("Cancel")) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (m_open_save_confirmation) {
			ImGui::OpenPopup("Save current scene?");
			m_open_save_confirmation = false;
		}
		if (ImGui::BeginPopupModal(
			"Save current scene?",
			nullptr,
			ImGuiWindowFlags_AlwaysAutoResize)) {
			ImGui::TextUnformatted("The current scene has unsaved changes.");
			if (ImGui::Button("Save and Continue")) {
				SaveScene();
				m_execute_document_action_after_save = m_context.scene.Saving();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel")) {
				m_pending_document_action = PendingDocumentAction::None;
				m_execute_document_action_after_save = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
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

		enum class PendingDocumentAction {
			None,
			New,
			Open
		};

		std::unique_ptr<fyuu_engine::AssetStore> m_asset_store;
		EditorContext m_context;
		std::vector<fyuu_engine::SceneAssetEntry> m_scene_assets;
		fyuu_asset::UUID m_pending_scene;
		PendingDocumentAction m_pending_document_action = PendingDocumentAction::None;
		bool m_open_scene_browser = false;
		bool m_open_save_confirmation = false;
		bool m_execute_document_action_after_save = false;
	};

	int RunApplication(int argc, char** argv) {
		EditorApplication editor;
		return editor.Run(argc, argv);
	}

}
