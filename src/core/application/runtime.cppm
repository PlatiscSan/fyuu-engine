module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <print>
#include <stdexcept>
#include <string>
#endif // !defined(__cpp_lib_modules)
#include <CLI/CLI.hpp>

export module fyuu_engine:runtime;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
export import :application_types;
import :log;
import :platform;
import :rendering_system;

namespace fyuu_engine {

	export class Runtime {
	private:
		ApplicationDescriptor m_application;
		Platform m_platform;
		RenderingSystem m_rendering;
		RuntimeState m_state = RuntimeState::Created;
		bool m_show_help = false;
		bool m_log_initialized = false;
		bool m_rendering_initialized = false;
		bool m_application_initialized = false;

	public:
		explicit Runtime(ApplicationDescriptor const& application)
			: m_application(application) {

		}

		Runtime(Runtime const&) = delete;
		Runtime& operator=(Runtime const&) = delete;
		Runtime(Runtime&&) = delete;
		Runtime& operator=(Runtime&&) = delete;

		~Runtime() noexcept {
			Shutdown();
		}

		void Initialize(int argc, char** argv) {
			if (m_state != RuntimeState::Created) {
				throw std::logic_error("Runtime can only be initialized once");
			}

			log::Initialize();
			m_log_initialized = true;
			CLI::App command_line(m_application.description, m_application.name);
			command_line.add_option(
				"--API",
				m_application.graphics_api,
				"Graphics API, can be Vulkan, D3D12, Metal, WebGPU, OpenGL"
			);
			command_line.add_option(
				"--conf",
				m_application.configuration_path,
				"Configuration file path (YAML or JSON)"
			);

			try {
				command_line.parse(argc, argv);
				std::ranges::transform(
					m_application.graphics_api,
					m_application.graphics_api.begin(),
					[](unsigned char character) {
						return static_cast<char>(std::tolower(character));
					}
				);
				m_platform.Initialize(m_application);
				m_rendering.Initialize(m_platform, m_application);
				m_rendering_initialized = true;
				m_state = RuntimeState::Initialized;
				if (m_application.Initialize) {
					m_application.Initialize(*this);
				}
				m_application_initialized = true;
				if (m_state == RuntimeState::Initialized) {
					m_state = RuntimeState::Running;
				}
				log::Info(std::format(
					"Engine initialized with graphics API: '{}', configuration file: '{}'",
					m_application.graphics_api,
					m_application.configuration_path.string()
				));
				return;
			}
			catch (CLI::CallForHelp const&) {
				m_show_help = true;
				std::println("{}", command_line.help());
				Shutdown();
				return;
			}
			catch (...) {
				Shutdown();
				throw;
			}
		}

		void Tick() {
			if (m_state != RuntimeState::Running) {
				return;
			}
			m_platform.ProcessEvents(m_application);
			if (m_platform.StopRequested()) {
				RequestStop();
			}
			if (m_state == RuntimeState::Running &&
				m_platform.SurfaceVisible() &&
				m_application.Tick) {
				m_application.Tick(*this);
			}
			if (m_state == RuntimeState::Running && m_platform.SurfaceVisible()) {
				m_rendering.Render(m_platform, m_application);
			}
		}

		void Run() {
			if (m_state != RuntimeState::Running) {
				throw std::logic_error("Runtime must be initialized before running");
			}
			while (m_state == RuntimeState::Running) {
				Tick();
			}
		}

		void RequestStop() noexcept {
			if (m_state == RuntimeState::Running || m_state == RuntimeState::Initialized) {
				m_state = RuntimeState::Stopping;
			}
		}

		void Shutdown() noexcept {
			if (m_state == RuntimeState::Stopped) {
				return;
			}
			if (m_application_initialized) {
				if (m_application.Shutdown) {
					try {
						m_application.Shutdown(*this);
					}
					catch (...) {
						log::Error("Application shutdown callback threw an exception");
					}
				}
				m_application_initialized = false;
			}
			if (m_rendering_initialized) {
				try {
					m_rendering.Shutdown();
				}
				catch (...) {
					log::Error("Rendering shutdown threw an exception");
				}
				m_rendering_initialized = false;
			}
			m_platform.Shutdown();
			m_state = RuntimeState::Stopped;
			if (m_log_initialized) {
				log::Info("Engine shutdown successfully");
				log::Shutdown();
				m_log_initialized = false;
			}
		}

		[[nodiscard]] RuntimeState State() const noexcept {
			return m_state;
		}

		[[nodiscard]] bool IsRunning() const noexcept {
			return m_state == RuntimeState::Running;
		}

		[[nodiscard]] bool ShowHelp() const noexcept {
			return m_show_help;
		}

		[[nodiscard]] ApplicationDescriptor const& Application() const noexcept {
			return m_application;
		}

		[[nodiscard]] void* UserData() const noexcept {
			return m_application.user_data;
		}
	};

}
