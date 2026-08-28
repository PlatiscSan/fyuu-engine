module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <limits>
#include <string>

#include <cstdint>
#include <atomic>

#include <optional>
#include <filesystem>

#include <format>
#endif // !defined(__cpp_lib_modules)
#include <SDL3/SDL.h>

module fyuu_desktop:platform;

import fyuu_desktop;
import fyuu_engine;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace {

	std::atomic_size_t s_video_reference_count = 0;

	bool AcquireVideoSubsystem() {
		auto const previous_count = s_video_reference_count.fetch_add(1, std::memory_order_acq_rel);
		if (previous_count == 0 &&
			!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
			s_video_reference_count.fetch_sub(1, std::memory_order_acq_rel);
			return false;
		}
		return true;
	}

	void ReleaseVideoSubsystem() noexcept {
		auto const previous_count = s_video_reference_count.fetch_sub(1, std::memory_order_acq_rel);
		if (previous_count == 1) {
			SDL_QuitSubSystem(SDL_INIT_VIDEO);
		}
	}

	fyuu_desktop::MouseButton TranslateMouseButton(std::uint8_t button) noexcept {
		switch (button) {
		case SDL_BUTTON_LEFT:
			return fyuu_desktop::MouseButton::Left;
		case SDL_BUTTON_MIDDLE:
			return fyuu_desktop::MouseButton::Middle;
		case SDL_BUTTON_RIGHT:
			return fyuu_desktop::MouseButton::Right;
		case SDL_BUTTON_X1:
			return fyuu_desktop::MouseButton::Extra1;
		case SDL_BUTTON_X2:
			return fyuu_desktop::MouseButton::Extra2;
		default:
			return fyuu_desktop::MouseButton::Unknown;
		}
	}

	fyuu_desktop::Key TranslateKey(SDL_Scancode scancode) noexcept {
		switch (scancode) {
		case SDL_SCANCODE_TAB: return fyuu_desktop::Key::Tab;
		case SDL_SCANCODE_LEFT: return fyuu_desktop::Key::LeftArrow;
		case SDL_SCANCODE_RIGHT: return fyuu_desktop::Key::RightArrow;
		case SDL_SCANCODE_UP: return fyuu_desktop::Key::UpArrow;
		case SDL_SCANCODE_DOWN: return fyuu_desktop::Key::DownArrow;
		case SDL_SCANCODE_PAGEUP: return fyuu_desktop::Key::PageUp;
		case SDL_SCANCODE_PAGEDOWN: return fyuu_desktop::Key::PageDown;
		case SDL_SCANCODE_HOME: return fyuu_desktop::Key::Home;
		case SDL_SCANCODE_END: return fyuu_desktop::Key::End;
		case SDL_SCANCODE_INSERT: return fyuu_desktop::Key::Insert;
		case SDL_SCANCODE_DELETE: return fyuu_desktop::Key::Delete;
		case SDL_SCANCODE_BACKSPACE: return fyuu_desktop::Key::Backspace;
		case SDL_SCANCODE_SPACE: return fyuu_desktop::Key::Space;
		case SDL_SCANCODE_RETURN: return fyuu_desktop::Key::Enter;
		case SDL_SCANCODE_ESCAPE: return fyuu_desktop::Key::Escape;
		case SDL_SCANCODE_A: return fyuu_desktop::Key::A;
		case SDL_SCANCODE_C: return fyuu_desktop::Key::C;
		case SDL_SCANCODE_V: return fyuu_desktop::Key::V;
		case SDL_SCANCODE_X: return fyuu_desktop::Key::X;
		case SDL_SCANCODE_Y: return fyuu_desktop::Key::Y;
		case SDL_SCANCODE_Z: return fyuu_desktop::Key::Z;
		default: return fyuu_desktop::Key::Unknown;
		}
	}

	void TranslateModifiers(SDL_Keymod modifiers, fyuu_desktop::Event& event) noexcept {
		event.control = (modifiers & SDL_KMOD_CTRL) != 0;
		event.shift = (modifiers & SDL_KMOD_SHIFT) != 0;
		event.alt = (modifiers & SDL_KMOD_ALT) != 0;
		event.super = (modifiers & SDL_KMOD_GUI) != 0;
	}

	fyuu_desktop::PresentationTarget GetPresentationTarget(SDL_Window* window) {
		auto const properties = SDL_GetWindowProperties(window);
#if defined(_WIN32)
		auto* native_window = SDL_GetPointerProperty(
			properties,
			SDL_PROP_WINDOW_WIN32_HWND_POINTER,
			nullptr
		);
		if (native_window) {
			return fyuu_desktop::Win32PresentationTarget{ native_window };
		}
#elif defined(__linux__)
		auto* wayland_display = SDL_GetPointerProperty(
			properties,
			SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER,
			nullptr
		);
		auto* wayland_surface = SDL_GetPointerProperty(
			properties,
			SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER,
			nullptr
		);
		if (wayland_display && wayland_surface) {
			return fyuu_desktop::WaylandPresentationTarget{
				wayland_display,
				wayland_surface
			};
		}
		auto* x11_display = SDL_GetPointerProperty(
			properties,
			SDL_PROP_WINDOW_X11_DISPLAY_POINTER,
			nullptr
		);
		auto const x11_window = SDL_GetNumberProperty(
			properties,
			SDL_PROP_WINDOW_X11_WINDOW_NUMBER,
			0
		);
		if (x11_display && x11_window != 0) {
			return fyuu_desktop::X11PresentationTarget{
				x11_display,
				static_cast<std::uint64_t>(x11_window)
			};
		}
#elif defined(__APPLE__)
		auto* native_window = SDL_GetPointerProperty(
			properties,
			SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
			nullptr
		);
		if (native_window) {
			return fyuu_desktop::CocoaPresentationTarget{ native_window };
		}
#endif
		throw fyuu_engine::Error{
			fyuu_engine::Result::PlatformError,
			"SDL did not provide a native presentation target"
		};
	}

}

namespace fyuu_desktop {

	EventSink::~EventSink() noexcept = default;

	void EventSink::ProcessEvent(Event const&) {
	}

	void Platform::Release() noexcept {
		if (m_window) {
			auto* window = m_window;
			SDL_StopTextInput(window);
			SDL_DestroyWindow(window);
			m_window = nullptr;
		}
		ReleaseVideoSubsystem();
	}

	Platform::Platform(
		Descriptor const& descriptor,
		EventSink& event_sink
	) : m_descriptor(descriptor),
		m_event_sink(&event_sink),
		m_window(
			[this]() {
				auto const maximum_extent = static_cast<std::uint32_t>(std::numeric_limits<int>::max());
				if (m_descriptor.title.empty() ||
					m_descriptor.width == 0 ||
					m_descriptor.height == 0 ||
					m_descriptor.width > maximum_extent ||
					m_descriptor.height > maximum_extent) {
					throw fyuu_engine::Error{
						fyuu_engine::Result::InvalidArgument,
						"Desktop window descriptor is invalid"
					};
				}
				if (!AcquireVideoSubsystem()) {
					throw fyuu_engine::Error{
						fyuu_engine::Result::PlatformError,
						std::format("SDL video initialization failed: {}", SDL_GetError())
					};
				}
				auto flags = SDL_WindowFlags{ 0 };
				if (m_descriptor.resizable) {
					flags |= SDL_WINDOW_RESIZABLE;
				}
				if (m_descriptor.high_pixel_density) {
					flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
				}
				auto window = SDL_CreateWindow(
					m_descriptor.title.c_str(),
					static_cast<int>(m_descriptor.width),
					static_cast<int>(m_descriptor.height),
					flags
				);
				if (!window) {
					auto const message = std::format("SDL window creation failed: {}", SDL_GetError());
					ReleaseVideoSubsystem();
					throw fyuu_engine::Error{ fyuu_engine::Result::PlatformError, message };
				}
				return window;
			}()) {
		if (!SDL_StartTextInput(m_window)) {
			auto const message = std::format("SDL text input initialization failed: {}", SDL_GetError());
			Release();
			throw fyuu_engine::Error{ fyuu_engine::Result::PlatformError, message };
		}
		int logical_width = 0;
		int logical_height = 0;
		if (!SDL_GetWindowSize(m_window, &logical_width, &logical_height)) {
			auto const message = std::format("SDL window size query failed: {}", SDL_GetError());
			Release();
			throw fyuu_engine::Error{ fyuu_engine::Result::PlatformError, message };
		}
		Event logical_size_event;
		logical_size_event.type = EventType::WindowResized;
		logical_size_event.window_ID = SDL_GetWindowID(m_window);
		logical_size_event.x = static_cast<float>(logical_width);
		logical_size_event.y = static_cast<float>(logical_height);
		m_event_sink->ProcessEvent(logical_size_event);
		int pixel_width = 0;
		int pixel_height = 0;
		if (!SDL_GetWindowSizeInPixels(m_window, &pixel_width, &pixel_height)) {
			auto const message = std::format("SDL window pixel-size query failed: {}", SDL_GetError());
			Release();
			throw fyuu_engine::Error{ fyuu_engine::Result::PlatformError, message };
		}
		Event pixel_size_event;
		pixel_size_event.type = EventType::WindowPixelSizeChanged;
		pixel_size_event.window_ID = SDL_GetWindowID(m_window);
		pixel_size_event.x = static_cast<float>(pixel_width);
		pixel_size_event.y = static_cast<float>(pixel_height);
		m_event_sink->ProcessEvent(pixel_size_event);
	}

	Platform::~Platform() noexcept {
		Release();
	}

	PresentationTarget Platform::GetPresentationTarget() const {
		return ::GetPresentationTarget(m_window);
	}

	void Platform::PumpEvents(bool& close_requested) {
		if (!Valid()) {
			throw fyuu_engine::Error{
				fyuu_engine::Result::InvalidState,
				"Desktop platform is not initialized"
			};
		}
		close_requested = false;
		auto const window_ID = SDL_GetWindowID(m_window);
		SDL_Event native_event{};
		while (SDL_PollEvent(&native_event)) {
			if (native_event.type == SDL_EVENT_QUIT ||
				(native_event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
					native_event.window.windowID == window_ID)) {
				close_requested = true;
			}
			Event event{};
			auto dispatch = true;
			switch (native_event.type) {
			case SDL_EVENT_WINDOW_RESIZED:
				event.type = EventType::WindowResized;
				event.window_ID = native_event.window.windowID;
				event.x = static_cast<float>(native_event.window.data1);
				event.y = static_cast<float>(native_event.window.data2);
				break;
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
				event.type = EventType::WindowPixelSizeChanged;
				event.window_ID = native_event.window.windowID;
				event.x = static_cast<float>(native_event.window.data1);
				event.y = static_cast<float>(native_event.window.data2);
				break;
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				event.type = EventType::WindowFocusGained;
				event.window_ID = native_event.window.windowID;
				break;
			case SDL_EVENT_WINDOW_FOCUS_LOST:
				event.type = EventType::WindowFocusLost;
				event.window_ID = native_event.window.windowID;
				break;
			case SDL_EVENT_WINDOW_MOUSE_LEAVE:
				event.type = EventType::WindowMouseLeave;
				event.window_ID = native_event.window.windowID;
				break;
			case SDL_EVENT_MOUSE_MOTION:
				event.type = EventType::MouseMoved;
				event.window_ID = native_event.motion.windowID;
				event.x = native_event.motion.x;
				event.y = native_event.motion.y;
				event.delta_x = native_event.motion.xrel;
				event.delta_y = native_event.motion.yrel;
				break;
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
				event.type = EventType::MouseButtonPressed;
				event.window_ID = native_event.button.windowID;
				event.mouse_button = TranslateMouseButton(native_event.button.button);
				event.x = native_event.button.x;
				event.y = native_event.button.y;
				event.click_count = native_event.button.clicks;
				break;
			case SDL_EVENT_MOUSE_BUTTON_UP:
				event.type = EventType::MouseButtonReleased;
				event.window_ID = native_event.button.windowID;
				event.mouse_button = TranslateMouseButton(native_event.button.button);
				event.x = native_event.button.x;
				event.y = native_event.button.y;
				event.click_count = native_event.button.clicks;
				break;
			case SDL_EVENT_MOUSE_WHEEL:
				event.type = EventType::MouseWheel;
				event.window_ID = native_event.wheel.windowID;
				event.delta_x = native_event.wheel.x;
				event.delta_y = native_event.wheel.y;
				break;
			case SDL_EVENT_KEY_DOWN:
				event.type = EventType::KeyPressed;
				event.window_ID = native_event.key.windowID;
				event.key = TranslateKey(native_event.key.scancode);
				TranslateModifiers(native_event.key.mod, event);
				event.repeat = native_event.key.repeat;
				break;
			case SDL_EVENT_KEY_UP:
				event.type = EventType::KeyReleased;
				event.window_ID = native_event.key.windowID;
				event.key = TranslateKey(native_event.key.scancode);
				TranslateModifiers(native_event.key.mod, event);
				break;
			case SDL_EVENT_TEXT_INPUT:
				event.type = EventType::TextInput;
				event.window_ID = native_event.text.windowID;
				event.text = native_event.text.text;
				break;
			default:
				dispatch = false;
				break;
			}
			if (dispatch && event.window_ID == window_ID) {
				m_event_sink->ProcessEvent(event);
			}
		}
	}

	std::string ClipboardText() {
		auto* text = SDL_GetClipboardText();
		if (text == nullptr)
			return {};
		std::string result{text};
		SDL_free(text);
		return result;
	}

	void ClipboardText(std::string_view text) {
		SDL_SetClipboardText(std::string{text}.c_str());
	}

	std::filesystem::path PreferencePath(
		std::string_view organization,
		std::string_view application
	) {
		auto const organization_text = std::string{organization};
		auto const application_text = std::string{application};
		auto* text = SDL_GetPrefPath(organization_text.c_str(), application_text.c_str());
		if (text == nullptr)
			throw fyuu_engine::Error{
				fyuu_engine::Result::PlatformError,
				std::format("SDL preference path query failed: {}", SDL_GetError())
			};
		auto const utf8 = std::u8string{
			reinterpret_cast<char8_t const*>(text),
			reinterpret_cast<char8_t const*>(text + std::char_traits<char>::length(text))
		};
		SDL_free(text);
		return std::filesystem::path{utf8};
	}

}
