module;
#include <version>
#include <cstdlib>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <mutex>
#include <format>
#include <print>
#endif // !defined(__cpp_lib_modules)
#include <SDL3/SDL.h>
#define SDL_MAIN_HANDLED 0
#include <SDL3/SDL_main.h>
#include "api_macro.h"
#include "fyuu_application.h"
module fyuu_engine:entry;
import :runtime;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

extern "C" {
	LIB_API int LIB_CALL Fyuu_Run(int argc, char** argv, Fyuu_App* app) NOEXCEPT {
		static std::mutex s_mutex;
		static fyuu_engine::ApplicationDescriptor* s_application = nullptr;
		std::unique_lock<std::mutex> lock(s_mutex);
		if (!app) {
			return EXIT_FAILURE;
		}
		app->request_stop = false;
		fyuu_engine::ApplicationDescriptor application{
			.description = app->description ? app->description : "",
			.name = app->name ? app->name : "FyuuApplication",
			.title = app->title ? app->title : "FyuuApplication",
			.surface_width = app->surface_width,
			.surface_height = app->surface_height,
			.version = {
				.variant = app->version.variant,
				.major = app->version.major,
				.minor = app->version.minor,
				.patch = app->version.patch
			},
			.font_size = app->font_size > 0.0f ? app->font_size : 13.0f,
			.user_data = app,
			.Initialize = [](fyuu_engine::Runtime& runtime) {
				auto application = static_cast<Fyuu_App*>(runtime.UserData());
				auto const& descriptor = runtime.Application();
				application->surface_width = descriptor.surface_width;
				application->surface_height = descriptor.surface_height;
				if (application->Init) {
					application->Init(application);
				}
			},
			.Tick = [](fyuu_engine::Runtime& runtime) {
				auto application = static_cast<Fyuu_App*>(runtime.UserData());
				auto const& descriptor = runtime.Application();
				application->surface_width = descriptor.surface_width;
				application->surface_height = descriptor.surface_height;
				if (application->Tick) {
					application->Tick(application);
				}
				if (application->request_stop) {
					runtime.RequestStop();
				}
			},
			.CloseRequested = [](fyuu_engine::Runtime& runtime) {
				auto application = static_cast<Fyuu_App*>(runtime.UserData());
				return !application->CloseRequested
					|| application->CloseRequested(application);
			},
			.Shutdown = [](fyuu_engine::Runtime& runtime) {
				auto application = static_cast<Fyuu_App*>(runtime.UserData());
				if (application->Shutdown) {
					application->Shutdown(application);
				}
			}
		};
		s_application = &application;
		return SDL_RunApp(
			argc, argv,
			[](int argc, char** argv) -> int {
				try {
					fyuu_engine::Runtime runtime(*s_application);
					runtime.Initialize(argc, argv);
					if (runtime.ShowHelp()) {
						return EXIT_SUCCESS;
					}
					runtime.Run();
					runtime.Shutdown();
					return EXIT_SUCCESS;
				}
				catch (std::exception const& ex) {
					std::println("Exception thrown in EngineMain(): {}\n", ex.what());
					return EXIT_FAILURE;
				}
				catch (...) {
					std::println("Unknown exception thrown in EngineMain()\n");
					return EXIT_FAILURE;
				}
			},
			nullptr
		);
	}
}
