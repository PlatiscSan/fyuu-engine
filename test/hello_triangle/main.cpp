#include <imgui.h>
#include "fyuu_application.h"
#include "fyuu_log.h"

int main(int argc, char** argv) {
	Fyuu_App app = {
		.description = "A simple application to test the engine.",
		.name = "FyuuApp",
		.title = "FyuuApp",
		.surface_width = 800,
		.surface_height = 600,
		.version = { 0, 1, 0, 0 },
		.user_data = nullptr,
		.Init = [](Fyuu_App* self) NOEXCEPT {
			FYUU_LOG_INFO("App initialized");
		},
		.Tick = [](Fyuu_App* self) NOEXCEPT {
			ImGui::ShowDemoWindow();

		},
		.Shutdown = [](Fyuu_App* self) NOEXCEPT {

		}
	};
	return Fyuu_Run(argc, argv, &app);
}
