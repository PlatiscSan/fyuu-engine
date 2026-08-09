module;
#include <version>
#include <cstdlib>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#include <imgui.h>
#include "fyuu_application.h"
#include "fyuu_log.h"

module fyuu_editor:application;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_engine;
import :asset_browser_panel;
import :context;
import :console_panel;
import :hierarchy_panel;
import :inspector_panel;
import :scene_panel;

namespace fyuu_editor {

	// Top-level editor orchestrator.
	// Process chain: main -> fyuu_editor::Run -> RunApplication -> EditorApplication::Run
	// -> Fyuu_Run -> static ABI callbacks -> instance methods -> panels/context/model.
	// This is the only editor type that knows both the C application ABI and the C++
	// editor document model; callbacks must catch every exception at that boundary.
	class EditorApplication {
	private:
		enum class PendingDocumentAction {
			None,
			New,
			Open,
			Close
		};

		Fyuu_App* m_application = nullptr;
		std::filesystem::path m_asset_root = std::filesystem::current_path() / "assets";
		std::optional<fyuu_asset::UUID> m_initial_scene;
		std::unique_ptr<fyuu_engine::AssetStore> m_asset_store;
		EditorContext m_context;
		std::vector<fyuu_engine::SceneAssetEntry> m_scene_assets;
		fyuu_asset::UUID m_pending_scene;
		PendingDocumentAction m_pending_document_action = PendingDocumentAction::None;
		bool m_open_scene_browser = false;
		bool m_open_save_confirmation = false;
		bool m_execute_document_action_after_save = false;
		std::string m_failure_message;
		bool m_faulted = false;

				arguments = ParseArguments(argc, argv);
			}
			catch (std::exception const& exception) {

				editor.ReportFailure(exception.what());
			}
			catch (...) {

				DrawFailureWindow();
				return;
			}
			UpdateSave();
			ProcessShortcuts();
			DrawMainMenu();
			DrawDocumentPopups();
			DrawDefaultLayout();
			if (m_context.show_demo_window) {

			static_cast<EditorApplication*>(application->user_data)->Shutdown();
		}

		// Called by the runtime when the platform requests close. Allows immediate close
		// for clean/faulted documents; otherwise schedules confirmation or post-save close.
		static Fyuu_Bool CloseRequestedCallback(Fyuu_App* application) NOEXCEPT {

			DrawDocumentStatus();
			ImGui::EndMainMenuBar();
		}

		// Called only by DrawMainMenu. Reads EditorScene asset/dirty/saving state and
		// renders the right-aligned document identifier without mutating state.
		void DrawDocumentStatus() {

					SelectedEntity() != nullptr)) {
					CreateChildEntity();
				}

				m_context.scene.CreateEntity("Camera");
				m_context.selected_entity = m_context.scene.CreateEntity("Entity");
			}
			m_context.Log("Fyuu Editor initialized");
			m_context.Log(std::format("Asset root: {}", m_asset_store->Root().string()));
			FYUU_LOG_INFO("Fyuu Editor initialized");
		}

		// Called only by TickCallback. Per-frame chain: UpdateSave -> shortcuts -> menu ->
		// modal workflow -> panel layout. Faulted state bypasses normal document access.
		void Tick() {
			if (m_faulted) {

					CreateChildEntity();
				}
				if (ImGui::MenuItem(
					"Duplicate",
					"Ctrl+D",
					false,
					SelectedEntity() != nullptr)) {

					DuplicateSelectedEntity();
				}
				if (ImGui::MenuItem(
					"Delete",
					"Delete",
					false,
					SelectedEntity() != nullptr)) {

					DeleteSelectedEntity();
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Edit")) {
				if (ImGui::MenuItem("Undo", "Ctrl+Z", false, m_context.CanUndo())) {

					SaveScene();
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Entity")) {
				if (ImGui::MenuItem("Create Empty")) {

		// Called by RequestNewScene or ExecutePendingDocumentAction after confirmation;
		// delegates reset/history cleanup to EditorContext::NewScene and logs success.
		void NewScene() {
			if (m_context.NewScene()) {
				m_context.Log("Created new scene");
			}
		}

				OpenPendingScene();
				return;
			}
			m_pending_document_action = PendingDocumentAction::Open;
			m_open_save_confirmation = true;
		}

		// Called after confirmation or successful save. Consumes exactly one pending
		// New/Open/Close action and dispatches NewScene, OpenPendingScene, or request_stop.
		void ExecutePendingDocumentAction() {
			auto const action = m_pending_document_action;
			m_pending_document_action = PendingDocumentAction::None;
			if (action == PendingDocumentAction::New) {

					ExecutePendingDocumentAction();
				}
				else if (m_execute_document_action_after_save) {

					RequestNewScene();
				}
				if (ImGui::MenuItem(
					"Open Scene...",
					"Ctrl+O",
					false,
					m_asset_store && !m_context.scene.Saving())) {

		// Called by open modal and Asset Browser double-click. Stores the target UUID,
		// opens immediately when clean, or defers Open behind save confirmation.
		void RequestOpenScene(fyuu_asset::UUID const& id) {
			if (m_context.scene.Saving()) {
				m_context.Log("Cannot open a scene while saving");
				return;
			}
			m_pending_scene = id;
			if (!m_context.scene.Dirty()) {
				OpenPendingScene();
				return;
			}
			m_pending_document_action = PendingDocumentAction::Open;
			m_open_save_confirmation = true;
		}

					OpenSceneBrowser();
				}
				if (ImGui::MenuItem(
					"Save Scene",
					"Ctrl+S",
					false,
					m_asset_store && !m_context.scene.Saving())) {

			UpdateSave();
			ProcessShortcuts();
			DrawMainMenu();
			DrawDocumentPopups();
			DrawDefaultLayout();
			if (m_context.show_demo_window) {
				ImGui::ShowDemoWindow(&m_context.show_demo_window);
			}

			DrawMainMenu();
			DrawDocumentPopups();
			DrawDefaultLayout();
			if (m_context.show_demo_window) {
				ImGui::ShowDemoWindow(&m_context.show_demo_window);
			}

			ProcessShortcuts();
			DrawMainMenu();
			DrawDocumentPopups();
			DrawDefaultLayout();
			if (m_context.show_demo_window) {
				ImGui::ShowDemoWindow(&m_context.show_demo_window);
			}

			DrawDocumentPopups();
			DrawDefaultLayout();
			if (m_context.show_demo_window) {
				ImGui::ShowDemoWindow(&m_context.show_demo_window);
			}

			DrawDefaultLayout();
			if (m_context.show_demo_window) {
				ImGui::ShowDemoWindow(&m_context.show_demo_window);
			}

				editor.Initialize();
			}
			catch (std::exception const& exception) {

				editor.Tick();
			}
			catch (std::exception const& exception) {

		// Called by the runtime when the platform requests close. Allows immediate close
		// for clean/faulted documents; otherwise schedules confirmation or post-save close.
		static Fyuu_Bool CloseRequestedCallback(Fyuu_App* application) NOEXCEPT {
			auto& editor = *static_cast<EditorApplication*>(application->user_data);
			if (editor.m_faulted || (!editor.m_context.scene.Dirty()
				&& !editor.m_context.scene.Saving())) {
				return true;
			}
			editor.m_pending_document_action = PendingDocumentAction::Close;
			if (editor.m_context.scene.Saving()) {
				editor.m_execute_document_action_after_save = true;
			}
			else {
				editor.m_open_save_confirmation = true;
			}
			return false;
		}

		// Called by Fyuu_Run through Fyuu_App::Init. Recovers this from user_data, stores
		// the ABI handle, calls Initialize, and converts exceptions into faulted UI state.
		static void InitializeCallback(Fyuu_App* application) NOEXCEPT {
			auto& editor = *static_cast<EditorApplication*>(application->user_data);
			editor.m_application = application;
			try {
				editor.Initialize();
			}
			catch (std::exception const& exception) {
				editor.ReportFailure(exception.what());
			}
			catch (...) {
				editor.ReportFailure("Unknown editor initialization error");
			}
		}

		// Called by Fyuu_Run once per frame through Fyuu_App::Tick. Dispatches Tick and
		// prevents C++ exceptions from crossing the C ABI.
		static void TickCallback(Fyuu_App* application) NOEXCEPT {
			auto& editor = *static_cast<EditorApplication*>(application->user_data);
			try {
				editor.Tick();
			}
			catch (std::exception const& exception) {
				editor.ReportFailure(exception.what());
			}
			catch (...) {
				editor.ReportFailure("Unknown editor frame error");
			}
		}

		// Called by Fyuu_Run through Fyuu_App::Shutdown after frame processing stops;
		// forwards to the instance shutdown hook.
		static void ShutdownCallback(Fyuu_App* application) NOEXCEPT {
			static_cast<EditorApplication*>(application->user_data)->Shutdown();
		}

	public:

		// Called by RunApplication. Parses editor-only options, builds the ABI descriptor,
		// and transfers frame/lifecycle control to Fyuu_Run.
		int Run(int argc, char** argv) {
			std::vector<char*> arguments;
			try {
				arguments = ParseArguments(argc, argv);
			}
			catch (std::exception const& exception) {
				std::println("Editor argument error: {}", exception.what());
				return EXIT_FAILURE;
			}
			Fyuu_App application{
				.description = "FyuuEngine scene editor",
				.name = "FyuuEditor",
				.title = "Fyuu Editor",
				.surface_width = 1440u,
				.surface_height = 900u,
				.version = {0u, 1u, 0u, 0u},
				.font_size = 16.0f,
				.user_data = this,
				.request_stop = false,
				.Init = InitializeCallback,
				.Tick = TickCallback,
				.CloseRequested = CloseRequestedCallback,
				.Shutdown = ShutdownCallback,
			};
			return Fyuu_Run(
				static_cast<int>(arguments.size()),
				arguments.data(),
				&application
				);
		}

	};

	// Called by the public fyuu_editor::Run entry. Owns EditorApplication for the full
	// synchronous Fyuu_Run lifetime and returns its process exit code.
	int RunApplication(int argc, char** argv) {
		EditorApplication editor;
		return editor.Run(argc, argv);
	}

}
