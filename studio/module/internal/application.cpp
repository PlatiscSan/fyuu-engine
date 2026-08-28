module;
#include <version>
#if !defined(__cpp_lib_modules)
// C++98: owned document title.
#include <vector>
#include <string>
// C++11: document revisions.
#include <cstdint>
// C++17: borrowed text parameters.
#include <string_view>
// C++20: diagnostic message formatting.
#include <format>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#endif // defined(_WIN32)

module fyuu_studio:application;

import fyuu_desktop;
import fyuu_engine;
import fyuu_rhi;
import fyuu_studio;
import fyuu_ui;
import :rhi_context;
import :ui;
import :ui_renderer;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

#if defined(_WIN32)
namespace {

	bool IsWindows10OrNewer() noexcept {
		using RtlGetVersion = LONG(WINAPI*)(OSVERSIONINFOW*);

		auto const module = GetModuleHandleW(L"ntdll.dll");
		if (module == nullptr) {
			return false;
		}
		auto const get_version = reinterpret_cast<RtlGetVersion>(
			GetProcAddress(module, "RtlGetVersion")
		);
		if (get_version == nullptr) {
			return false;
		}
		OSVERSIONINFOW version{};
		version.dwOSVersionInfoSize = sizeof(version);
		return get_version(&version) >= 0 &&
			version.dwMajorVersion >= 10u;
	}

}
#endif // defined(_WIN32)

namespace fyuu_studio {

	class StudioLogSink final : public fyuu_engine::LogSink {
	private:
		fyuu_engine::ConsoleLogSink m_console_sink;

	public:
		void Write(fyuu_engine::LogRecord const& record) override {
			m_console_sink.Write(record);
		}
	};

	/// Owns services shared by StudioApplication and future panels.
	/// Construction order guarantees Logger is destroyed before its borrowed StudioLogSink.
	class StudioContext {
	private:
		StudioLogSink m_log_sink;
		fyuu_engine::Logger m_logger;

	public:
		StudioContext()
			: m_logger(m_log_sink) {
		}

		StudioContext(StudioContext const&) = delete;
		StudioContext& operator=(StudioContext const&) = delete;
		StudioContext(StudioContext&&) = delete;
		StudioContext& operator=(StudioContext&&) = delete;

		fyuu_engine::Logger& GetLogger() noexcept {
			return m_logger;
		}

	};

	class StudioApplication final : public fyuu_engine::Application {
	private:
		StudioContext* m_context = nullptr;
		StudioUI* m_ui = nullptr;
		UIRenderer* m_renderer = nullptr;
		std::vector<StudioCommand> m_commands;
		bool m_started = false;

	public:
		StudioApplication(
			StudioContext& context,
			StudioUI& ui,
			UIRenderer& renderer
		) noexcept
			: m_context(&context),
			m_ui(&ui),
			m_renderer(&renderer) {
		}

		StudioApplication(StudioApplication const&) = delete;
		StudioApplication& operator=(StudioApplication const&) = delete;
		StudioApplication(StudioApplication&&) = delete;
		StudioApplication& operator=(StudioApplication&&) = delete;

		/// Records the first successful Studio frame, then becomes the future UI update entry.
		void Tick(fyuu_engine::Runtime& runtime) override {
			if (!m_started) {
				m_context->GetLogger().Write(
					fyuu_engine::LogLevel::Info,
					"Studio",
					"Studio application started"
				);
				m_started = true;
			}
			m_ui->DrainCommands(m_commands);
			for (auto const command : m_commands) {
				switch (command) {
				case StudioCommand::NewDocument:
					m_ui->ResetDocument();
					break;
				case StudioCommand::SaveDocument:
					m_context->GetLogger().Write(
						fyuu_engine::LogLevel::Info,
						"Studio",
						"Scene asset saving is not connected yet"
					);
					break;
				}
			}
			m_renderer->Submit(
				m_ui->BuildDrawList(),
				m_ui->GetLogicalWidth(),
				m_ui->GetLogicalHeight(),
				m_ui->GetPixelWidth(),
				m_ui->GetPixelHeight()
			);
		}

		/// Accepts the close request until asset-backed editing supplies real save state.
		bool CloseRequested(fyuu_engine::Runtime&) override {
			m_context->GetLogger().Write(
				fyuu_engine::LogLevel::Info,
				"Studio",
				"Studio close requested"
			);
			return true;
		}
	};

	void RunBackend(
		StudioContext& context,
		StudioUI& ui,
		fyuu_desktop::Platform& platform,
		fyuu_desktop::PresentationTarget const& presentation_target,
		fyuu_rhi::Backend backend
	) {
		RHIContext rhi_context{ backend, presentation_target };
		UIRenderer renderer{ rhi_context };
		StudioApplication application{ context, ui, renderer };
		context.GetLogger().Write(
			fyuu_engine::LogLevel::Info,
			"Studio",
			"Studio RHI device and command scheduler initialized"
		);
		fyuu_engine::Runtime runtime{ platform, context.GetLogger(), application };
		runtime.Run();
	}

	RenderBackend DefaultRenderBackend() noexcept {
#if defined(__APPLE__)
		return RenderBackend::Metal;
#elif defined(_WIN32)
		if (IsWindows10OrNewer()) {
			return RenderBackend::D3D12;
		}
		return RenderBackend::Vulkan;
#else
		return RenderBackend::Vulkan;
#endif
	}

	RenderBackend ParseRenderBackend(std::string_view name) {
		if (name == "d3d12") {
			return RenderBackend::D3D12;
		}
		if (name == "vulkan") {
			return RenderBackend::Vulkan;
		}
		if (name == "opengl") {
			return RenderBackend::OpenGL;
		}
		if (name == "webgpu") {
			return RenderBackend::WebGPU;
		}
		if (name == "metal") {
			return RenderBackend::Metal;
		}
		throw fyuu_engine::Error{
			fyuu_engine::Result::InvalidArgument,
			std::format("Unknown Studio RHI backend '{}'", name)
		};
	}

	std::string_view RenderBackendName(RenderBackend backend) {
		switch (backend) {
		case RenderBackend::D3D12:
			return "D3D12";
		case RenderBackend::Vulkan:
			return "Vulkan";
		case RenderBackend::OpenGL:
			return "OpenGL";
		case RenderBackend::WebGPU:
			return "WebGPU";
		case RenderBackend::Metal:
			return "Metal";
		default:
			return "Unknown";
		}
	}

	void Run(RenderBackend backend) {
		fyuu_desktop::Descriptor const descriptor{
			"Fyuu Studio",
			1600,
			900,
			true,
			true
		};
		StudioContext context;
		StudioUI ui{ descriptor.width, descriptor.height, RenderBackendName(backend) };
		fyuu_desktop::Platform platform{ descriptor, ui };
		auto const& presentation_target = platform.GetPresentationTarget();
		switch (backend) {
#if defined(_WIN32)
		case RenderBackend::D3D12:
			RunBackend(
				context,
				ui,
				platform,
				presentation_target,
				fyuu_rhi::Backend::DirectX12
			);
			return;
#endif
#if !defined(__APPLE__)
		case RenderBackend::Vulkan:
			RunBackend(
				context,
				ui,
				platform,
				presentation_target,
				fyuu_rhi::Backend::Vulkan
			);
			return;
		case RenderBackend::OpenGL:
			RunBackend(
				context,
				ui,
				platform,
				presentation_target,
				fyuu_rhi::Backend::OpenGL
			);
			return;
#endif
		case RenderBackend::WebGPU:
			RunBackend(
				context,
				ui,
				platform,
				presentation_target,
				fyuu_rhi::Backend::WebGPU
			);
			return;
#if defined(__APPLE__)
		case RenderBackend::Metal:
			RunBackend(
				context,
				ui,
				platform,
				presentation_target,
				fyuu_rhi::Backend::Metal
			);
			return;
#endif
		default:
			throw fyuu_engine::Error{
				fyuu_engine::Result::InvalidArgument,
				"Selected Studio RHI backend is unavailable on this platform"
			};
		}
	}

}
