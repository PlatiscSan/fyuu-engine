module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>
#include <string_view>
#include <filesystem>

#include <cstdint>

#include <optional>
#include <variant>
#endif // !defined(__cpp_lib_modules)

#include <SDL3/SDL.h>

export module fyuu_desktop;

import fyuu_engine;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_desktop {

	/// Describes the main desktop window created by Platform's constructor.
	/// The title is owned by the descriptor and remains valid for the platform lifetime.
	struct Descriptor {
		std::string title = "Fyuu";
		std::uint32_t width = 1280;
		std::uint32_t height = 720;
		bool resizable = true;
		bool high_pixel_density = true;
	};

	struct Win32PresentationTarget {
		void* window = nullptr;
	};

	struct X11PresentationTarget {
		void* display = nullptr;
		std::uint64_t window = 0;
	};

	struct WaylandPresentationTarget {
		void* display = nullptr;
		void* surface = nullptr;
	};

	struct CocoaPresentationTarget {
		void* window = nullptr;
	};

	using PresentationTarget = std::variant<
		Win32PresentationTarget,
		X11PresentationTarget,
		WaylandPresentationTarget,
		CocoaPresentationTarget
	>;

	enum class EventType {
		WindowResized,
		WindowPixelSizeChanged,
		WindowFocusGained,
		WindowFocusLost,
		WindowMouseLeave,
		MouseMoved,
		MouseButtonPressed,
		MouseButtonReleased,
		MouseWheel,
		KeyPressed,
		KeyReleased,
		TextInput
	};

	enum class MouseButton {
		Unknown,
		Left,
		Middle,
		Right,
		Extra1,
		Extra2
	};

	enum class Key {
		Unknown,
		Tab,
		LeftArrow,
		RightArrow,
		UpArrow,
		DownArrow,
		PageUp,
		PageDown,
		Home,
		End,
		Insert,
		Delete,
		Backspace,
		Space,
		Enter,
		Escape,
		A,
		C,
		V,
		X,
		Y,
		Z
	};

	/// Normalized desktop input delivered without exposing SDL types to Studio.
	struct Event {
		EventType type = EventType::WindowResized;
		std::uint32_t window_ID = 0;
		MouseButton mouse_button = MouseButton::Unknown;
		Key key = Key::Unknown;
		float x = 0.0f;
		float y = 0.0f;
		float delta_x = 0.0f;
		float delta_y = 0.0f;
		bool repeat = false;
		bool control = false;
		bool shift = false;
		bool alt = false;
		bool super = false;
		std::uint8_t click_count = 0u;
		std::string text;
	};

	/// Returns and replaces UTF-8 clipboard text through the active desktop backend.
	[[nodiscard]] std::string ClipboardText();
	void ClipboardText(std::string_view text);
	/// Returns a writable per-user directory selected by the desktop platform.
	[[nodiscard]] std::filesystem::path PreferencePath(
		std::string_view organization,
		std::string_view application
	);

	/// Receives normalized events during Platform::PumpEvents.
	struct EventSink {
		virtual ~EventSink() noexcept;
		virtual void ProcessEvent(Event const& event);
	};

	/// Owns the SDL3 desktop resources and adapts them to fyuu_engine::Platform.
	/// Call chain: Runtime::Tick -> C ABI -> generated thunk -> PumpEvents -> SDL_PollEvent.
	class Platform final : public fyuu_engine::Platform {
	private:
		Descriptor m_descriptor{};
		EventSink* m_event_sink = nullptr;
		SDL_Window* m_window = nullptr;

		void Release() noexcept;
		void PumpEvents(bool& close_requested) override;

	public:
		/// Copies the descriptor, acquires the shared SDL video subsystem, and creates the main window.
		Platform(Descriptor const& descriptor, EventSink& event_sink);
		/// Destroys the main window and releases this platform's SDL video reference.
		~Platform() noexcept override;

		Platform(Platform const&) = delete;
		Platform& operator=(Platform const&) = delete;
		Platform(Platform&&) = delete;
		Platform& operator=(Platform&&) = delete;

		/// Returns the backend-independent native target required by an RHI presentation surface.
		PresentationTarget GetPresentationTarget() const;
	};

	/// Runs one Application with a borrowed Logger and SDL desktop platform until it stops or throws.
	/// Call chain: Run -> Platform constructor -> Runtime constructor -> Runtime::Run.
	/// Destruction order is Runtime then Platform, preserving the borrowed platform lifetime.
	void Run(
		Descriptor const& descriptor,
		fyuu_engine::Logger& logger,
		EventSink& event_sink,
		fyuu_engine::Application& application
	);

}
