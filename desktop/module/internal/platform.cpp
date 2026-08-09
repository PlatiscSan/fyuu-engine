module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
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

}

namespace fyuu_desktop {

	void Platform::Release() noexcept {
		if (m_window) {
			SDL_DestroyWindow(static_cast<SDL_Window*>(m_window));
			m_window = nullptr;
		}
		ReleaseVideoSubsystem();
	}

	Platform::Platform(Descriptor const& descriptor)
		: m_descriptor(descriptor) {
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
		m_window = SDL_CreateWindow(
			m_descriptor.title.c_str(),
			static_cast<int>(m_descriptor.width),
			static_cast<int>(m_descriptor.height),
			flags
		);
		if (!m_window) {
			auto const message = std::format("SDL window creation failed: {}", SDL_GetError());
			ReleaseVideoSubsystem();
			throw fyuu_engine::Error{ fyuu_engine::Result::PlatformError, message };
		}
	}

	Platform::~Platform() noexcept {
		Release();
	}

	bool Platform::Valid() const noexcept {
		return m_window;
	}

	void Platform::PumpEvents(bool& close_requested) {
		if (!Valid()) {
			throw fyuu_engine::Error{
				fyuu_engine::Result::InvalidState,
				"Desktop platform is not initialized"
			};
		}
		close_requested = false;
		auto const window_ID = SDL_GetWindowID(static_cast<SDL_Window*>(m_window));
		SDL_Event event{};
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT ||
				(event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
					event.window.windowID == window_ID)) {
				close_requested = true;
			}
		}
	}

}
