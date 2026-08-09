module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdint>
#include <string>
#endif // !defined(__cpp_lib_modules)

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

	/// Owns the SDL3 desktop resources and adapts them to fyuu_engine::Platform.
	/// Call chain: Runtime::Tick -> C ABI -> generated thunk -> PumpEvents -> SDL_PollEvent.
	class Platform final : public fyuu_engine::Platform {
	private:
		Descriptor m_descriptor{};
		void* m_window = nullptr;

		void Release() noexcept;
		void PumpEvents(bool& close_requested) override;

	public:
		/// Copies the descriptor, acquires the shared SDL video subsystem, and creates the main window.
		explicit Platform(Descriptor const& descriptor);
		/// Destroys the main window and releases this platform's SDL video reference.
		~Platform() noexcept override;

		Platform(Platform const&) = delete;
		Platform& operator=(Platform const&) = delete;
		Platform(Platform&&) = delete;
		Platform& operator=(Platform&&) = delete;

		/// Returns true after SDL video and the main window have been initialized.
		bool Valid() const noexcept;
	};

	/// Runs one Application with an SDL desktop platform until it stops or returns an error.
	/// Call chain: Run -> Platform constructor -> Runtime constructor -> Runtime::Run.
	/// Destruction order is Runtime then Platform, preserving the borrowed platform lifetime.
	void Run(
		Descriptor const& descriptor,
		fyuu_engine::Application& application
	);

}
