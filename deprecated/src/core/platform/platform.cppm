module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <chrono>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <utility>
#endif // !defined(__cpp_lib_modules)
#include <SDL3/SDL.h>
#include <imgui.h>

module fyuu_engine:platform;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :application_types;

namespace fyuu_engine {
	namespace {
		ImGuiKey ImGuiKeyFromSDL(SDL_Keycode key) noexcept {
			switch (key) {
			case SDLK_TAB: return ImGuiKey_Tab;
			case SDLK_LEFT: return ImGuiKey_LeftArrow;
			case SDLK_RIGHT: return ImGuiKey_RightArrow;
			case SDLK_UP: return ImGuiKey_UpArrow;
			case SDLK_DOWN: return ImGuiKey_DownArrow;
			case SDLK_PAGEUP: return ImGuiKey_PageUp;
			case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
			case SDLK_HOME: return ImGuiKey_Home;
			case SDLK_END: return ImGuiKey_End;
			case SDLK_INSERT: return ImGuiKey_Insert;
			case SDLK_DELETE: return ImGuiKey_Delete;
			case SDLK_BACKSPACE: return ImGuiKey_Backspace;
			case SDLK_SPACE: return ImGuiKey_Space;
			case SDLK_RETURN: return ImGuiKey_Enter;
			case SDLK_ESCAPE: return ImGuiKey_Escape;
			case SDLK_A: return ImGuiKey_A;
			case SDLK_B: return ImGuiKey_B;
			case SDLK_C: return ImGuiKey_C;
			case SDLK_D: return ImGuiKey_D;
			case SDLK_E: return ImGuiKey_E;
			case SDLK_F: return ImGuiKey_F;
			case SDLK_G: return ImGuiKey_G;
			case SDLK_H: return ImGuiKey_H;
			case SDLK_I: return ImGuiKey_I;
			case SDLK_J: return ImGuiKey_J;
			case SDLK_K: return ImGuiKey_K;
			case SDLK_L: return ImGuiKey_L;
			case SDLK_M: return ImGuiKey_M;
			case SDLK_N: return ImGuiKey_N;
			case SDLK_O: return ImGuiKey_O;
			case SDLK_P: return ImGuiKey_P;
			case SDLK_Q: return ImGuiKey_Q;
			case SDLK_R: return ImGuiKey_R;
			case SDLK_S: return ImGuiKey_S;
			case SDLK_T: return ImGuiKey_T;
			case SDLK_U: return ImGuiKey_U;
			case SDLK_V: return ImGuiKey_V;
			case SDLK_W: return ImGuiKey_W;
			case SDLK_X: return ImGuiKey_X;
			case SDLK_Y: return ImGuiKey_Y;
			case SDLK_Z: return ImGuiKey_Z;
			default: return ImGuiKey_None;
			}
		}
	}

	class Platform {
	private:
		SDL_Window* m_main_window = nullptr;
		bool m_initialized = false;
		bool m_surface_visible = true;
		bool m_stop_requested = false;
		bool m_resize_pending = false;
		std::uint32_t m_pending_width = 0u;
		std::uint32_t m_pending_height = 0u;
		std::chrono::steady_clock::time_point m_last_resize;

		void QueueWindowExtent() {
			int width;
			int height;
			if (!SDL_GetWindowSizeInPixels(m_main_window, &width, &height)) {
				throw std::runtime_error(std::format(
					"Calling SDL_GetWindowSizeInPixels(), SDL reports {}",
					SDL_GetError()
				));
			}
			if (width <= 0 || height <= 0) {
				return;
			}
			m_pending_width = static_cast<std::uint32_t>(width);
			m_pending_height = static_cast<std::uint32_t>(height);
			m_resize_pending = true;
			m_last_resize = std::chrono::steady_clock::now();
		}

		void ApplyWindowExtent(ApplicationDescriptor& application) {
			if (!m_resize_pending ||
				std::chrono::steady_clock::now() - m_last_resize <
				std::chrono::milliseconds(100u)) {
				return;
			}
			application.surface_width = m_pending_width;
			application.surface_height = m_pending_height;
			m_resize_pending = false;
		}

	public:
		Platform() noexcept = default;

		Platform(Platform const&) = delete;
		Platform& operator=(Platform const&) = delete;

		Platform(Platform&& other) noexcept
			: m_main_window(std::exchange(other.m_main_window, nullptr)),
			m_initialized(std::exchange(other.m_initialized, false)),
			m_surface_visible(other.m_surface_visible),
			m_stop_requested(other.m_stop_requested),
			m_resize_pending(other.m_resize_pending),
			m_pending_width(other.m_pending_width),
			m_pending_height(other.m_pending_height),
			m_last_resize(other.m_last_resize) {

		}

		Platform& operator=(Platform&& other) noexcept {
			if (this != &other) {
				Shutdown();
				m_main_window = std::exchange(other.m_main_window, nullptr);
				m_initialized = std::exchange(other.m_initialized, false);
				m_surface_visible = other.m_surface_visible;
				m_stop_requested = other.m_stop_requested;
				m_resize_pending = other.m_resize_pending;
				m_pending_width = other.m_pending_width;
				m_pending_height = other.m_pending_height;
				m_last_resize = other.m_last_resize;
			}
			return *this;
		}

		~Platform() noexcept {
			Shutdown();
		}

		void Initialize(ApplicationDescriptor const& application) {
			if (m_initialized) {
				throw std::logic_error("Platform can only be initialized once");
			}
			if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
				throw std::runtime_error(std::format(
					"Calling SDL_InitSubSystem(), SDL reports {}",
					SDL_GetError()
				));
			}
			m_initialized = true;
			static constexpr SDL_WindowFlags window_flags =
				SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
			m_main_window = SDL_CreateWindow(
				application.title.c_str(),
				static_cast<int>(application.surface_width),
				static_cast<int>(application.surface_height),
				window_flags
			);
			if (!m_main_window) {
				auto error = std::runtime_error(std::format(
					"Calling SDL_CreateWindow(), SDL reports {}",
					SDL_GetError()
				));
				Shutdown();
				throw error;
			}
			if (!SDL_StartTextInput(m_main_window)) {
				auto error = std::runtime_error(std::format(
					"Calling SDL_StartTextInput(), SDL reports {}", SDL_GetError()
				));
				Shutdown();
				throw error;
			}
		}

		void ProcessEvents(ApplicationDescriptor& application) {
			SDL_Event event;
			while (SDL_PollEvent(&event)) {
				if (ImGui::GetCurrentContext()) {
					auto& io = ImGui::GetIO();
					switch (event.type) {
					case SDL_EVENT_MOUSE_WHEEL:
						io.AddMouseWheelEvent(event.wheel.x, event.wheel.y);
						break;
					case SDL_EVENT_TEXT_INPUT:
						io.AddInputCharactersUTF8(event.text.text);
						break;
					case SDL_EVENT_KEY_DOWN:
					case SDL_EVENT_KEY_UP: {
						bool down = event.type == SDL_EVENT_KEY_DOWN;
						auto key = ImGuiKeyFromSDL(event.key.key);
						if (key != ImGuiKey_None) io.AddKeyEvent(key, down);
						io.AddKeyEvent(ImGuiMod_Ctrl, (event.key.mod & SDL_KMOD_CTRL) != 0u);
						io.AddKeyEvent(ImGuiMod_Shift, (event.key.mod & SDL_KMOD_SHIFT) != 0u);
						io.AddKeyEvent(ImGuiMod_Alt, (event.key.mod & SDL_KMOD_ALT) != 0u);
						io.AddKeyEvent(ImGuiMod_Super, (event.key.mod & SDL_KMOD_GUI) != 0u);
						break;
					}
					case SDL_EVENT_WINDOW_FOCUS_GAINED:
						io.AddFocusEvent(true);
						break;
					case SDL_EVENT_WINDOW_FOCUS_LOST:
						io.AddFocusEvent(false);
						break;
					default:
						break;
					}
				}
				switch (event.type) {
				case SDL_EVENT_QUIT:
					m_stop_requested = true;
					break;
				case SDL_EVENT_WINDOW_RESIZED:
					QueueWindowExtent();
					break;
				case SDL_EVENT_WINDOW_MINIMIZED:
					m_surface_visible = false;
					m_resize_pending = false;
					break;
				case SDL_EVENT_WINDOW_RESTORED:
					m_surface_visible = true;
					QueueWindowExtent();
					break;
				case SDL_EVENT_WINDOW_MAXIMIZED:
					m_surface_visible = true;
					QueueWindowExtent();
					break;
				default:
					break;
				}
			}
			ApplyWindowExtent(application);
		}

		void Shutdown() noexcept {
			if (m_main_window) {
				SDL_DestroyWindow(m_main_window);
				m_main_window = nullptr;
			}
			if (m_initialized) {
				SDL_QuitSubSystem(SDL_INIT_VIDEO);
				m_initialized = false;
			}
		}

		[[nodiscard]] bool StopRequested() const noexcept {
			return m_stop_requested;
		}

		void ClearStopRequest() noexcept {
			m_stop_requested = false;
		}

		[[nodiscard]] bool SurfaceVisible() const noexcept {
			return m_surface_visible && !m_resize_pending;
		}

		[[nodiscard]] SDL_Window* MainWindow() const noexcept {
			return m_main_window;
		}

#if defined(_WIN32)
		[[nodiscard]] void* NativeWindow() const {
			auto properties = SDL_GetWindowProperties(m_main_window);
			if (!properties) {
				throw std::runtime_error(std::format(
					"Calling SDL_GetWindowProperties(), SDL reports {}",
					SDL_GetError()
				));
			}
			auto window = SDL_GetPointerProperty(
				properties,
				SDL_PROP_WINDOW_WIN32_HWND_POINTER,
				nullptr
			);
			if (!window) {
				throw std::runtime_error("SDL did not provide a Win32 window handle");
			}
			return window;
		}
#endif // defined(_WIN32)
	};

}
