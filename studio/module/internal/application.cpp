module;
#include <version>
#if !defined(__cpp_lib_modules)
// C++98: command storage.
#include <vector>
#include <string>
// C++17: borrowed text parameters.
#include <string_view>
// C++20: diagnostic message formatting.
#include <format>
#include <filesystem>
#include <optional>
#include <utility>
#include <ranges>
#endif // !defined(__cpp_lib_modules)
#include <coroutine>
#if defined(_WIN32)
#include <Windows.h>
#endif // defined(_WIN32)

module fyuu_studio:application;

import fyuu_desktop;
import fyuu_engine;
import fyuu_asset;
import fyuu_rhi;
import fyuu_studio;
import fyuu_ui;
import :rhi_context;
import :project;
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
		using SceneAsset = fyuu_asset::Asset<fyuu_asset::Scene>;
		using BitmapAsset = fyuu_asset::Asset<fyuu_asset::Bitmap>;
		using SaveTask = decltype(std::declval<SceneAsset>().Save());

		StudioContext* m_context = nullptr;
		StudioUI* m_ui = nullptr;
		UIRenderer* m_renderer = nullptr;
		SceneAsset::ManagedAsset m_scene;
		BitmapAsset::ManagedAsset m_scene_texture;
		std::optional<SaveTask> m_save;
		std::optional<Project> m_project;
		std::vector<StudioCommand> m_commands;
		std::filesystem::path m_project_path;
		bool m_started = false;

		void CreateFirstTexture() {
			static constexpr std::uint32_t Extent = 256u;
			std::vector<std::byte> pixels(Extent * Extent);
			std::ranges::for_each(std::views::iota(0u, Extent * Extent), [&](std::uint32_t pixel) {
				auto const x = pixel % Extent;
				auto const y = pixel / Extent;
				auto const bright = ((x / 32u) + (y / 32u)) % 2u == 0u;
				pixels[pixel] = static_cast<std::byte>(bright ? 224u : 48u);
			});
			m_scene_texture = BitmapAsset::Create(fyuu_asset::Bitmap{
				Extent, Extent, fyuu_asset::BitmapFormat::R8, std::move(pixels)
			});
			auto save = m_scene_texture->Save();
			save.Wait();
			m_renderer->SetSceneTexture(m_scene_texture->Get());
			m_context->GetLogger().Write(
				fyuu_engine::LogLevel::Info,
				"Studio",
				std::format("Created first texture asset {}", m_scene_texture->GetID().ToString())
			);
		}

		void LoadFirstTexture(std::filesystem::path const& asset_path) {
			auto const directory = asset_path / "Bitmap";
			std::error_code error;
			if (!std::filesystem::is_directory(directory, error)) {
				CreateFirstTexture();
				return;
			}
			std::vector<std::filesystem::path> textures;
			for (std::filesystem::directory_iterator iterator{directory, error}, end;
			     !error && iterator != end; iterator.increment(error)) {
				if (iterator->is_regular_file(error) && iterator->path().extension() == ".json")
					textures.emplace_back(iterator->path());
			}
			if (textures.empty()) {
				CreateFirstTexture();
				return;
			}
			std::ranges::sort(textures);
			auto const id = fyuu_asset::UUID::Parse(textures.front().stem().string());
			m_scene_texture = fyuu_asset::execution::AssetLoader{}.Load<fyuu_asset::Bitmap>(id);
			m_renderer->SetSceneTexture(m_scene_texture->Get());
		}

		void FinishSave() {
			if (!m_save || !m_save->IsDone()) {
				return;
			}
			try {
				m_save->Wait();
				m_context->GetLogger().Write(
					fyuu_engine::LogLevel::Info,
					"Studio",
					"Scene asset saved"
				);
			}
			catch (std::exception const& exception) {
				auto message = std::format("Unable to save scene asset: {}", exception.what());
				m_context->GetLogger().Write(
					fyuu_engine::LogLevel::Error,
					"Studio",
					message
				);
				m_ui->ShowSceneError(message);
			}
			m_save.reset();
		}

		void NewScene() {
			if (m_project_path.empty()) {
				throw std::runtime_error("No project is open");
			}
			m_scene = SceneAsset::Create();
			m_ui->SetSceneStatus("New Scene");
			m_context->GetLogger().Write(
				fyuu_engine::LogLevel::Info,
				"Studio",
				std::format("Created scene asset {}", m_scene->GetID().ToString())
			);
		}

		void OpenScene(std::filesystem::path const& path) {
			if (m_project_path.empty()) {
				throw std::runtime_error("No project is open");
			}
			if (path.parent_path().filename() != "Scene") {
				throw std::runtime_error("Scene asset must be stored in the Scene directory");
			}
			fyuu_asset::SetRoot(path.parent_path().parent_path());
			auto const id = fyuu_asset::UUID::Parse(path.stem().string());
			m_scene = fyuu_asset::execution::AssetLoader{}.Load<fyuu_asset::Scene>(id);
			m_ui->SetSceneStatus("Opened Scene");
			m_context->GetLogger().Write(
				fyuu_engine::LogLevel::Info,
				"Studio",
				std::format("Opened scene asset {}", id.ToString())
			);
		}

		void SaveScene() {
			if (!m_scene) {
				throw std::runtime_error("No scene asset is open");
			}
			if (m_save) {
				throw std::runtime_error("A scene save is already in progress");
			}
			m_save.emplace(m_scene->Save());
		}

		void NewProject(
			std::filesystem::path const& path,
			std::string_view name,
			std::filesystem::path const& asset_directory
		) {
			if (std::filesystem::exists(path)) {
				throw std::runtime_error("Project file already exists");
			}
			Project project{name, asset_directory};
			auto const resolved_asset_path = project.ResolveAssetPath(path);
			if (!std::filesystem::create_directories(resolved_asset_path)) {
				throw std::runtime_error("Unable to create project asset directory");
			}
			project.Serialize(path);
			m_project_path = std::filesystem::weakly_canonical(path);
			m_project.emplace(std::move(project));
			fyuu_asset::SetRoot(resolved_asset_path);
			CreateFirstTexture();
			m_ui->EnterProject(m_project_path, resolved_asset_path);
		}

		void OpenProject(std::filesystem::path const& path) {
			auto const project_path = std::filesystem::weakly_canonical(path);
			auto project = Project::Deserialize(project_path);
			auto const asset_path = project.ResolveAssetPath(project_path);
			if (!std::filesystem::is_directory(asset_path)) {
				throw std::runtime_error("Project asset directory does not exist");
			}
			m_project_path = project_path;
			m_project.emplace(std::move(project));
			fyuu_asset::SetRoot(asset_path);
			LoadFirstTexture(asset_path);
			m_ui->EnterProject(m_project_path, asset_path);
		}

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
			FinishSave();
			if (!m_started) {
				m_context->GetLogger().Write(
					fyuu_engine::LogLevel::Info,
					"Studio",
					"Studio application started"
				);
				m_started = true;
			}
			m_ui->DrainCommands(m_commands);
			for (auto const& command : m_commands) {
				try {
					switch (command.type) {
					case StudioCommandType::NewProject:
						NewProject(command.path, command.project_name, command.asset_path);
						break;
					case StudioCommandType::OpenProject:
						OpenProject(command.path);
						break;
					case StudioCommandType::NewDocument:
						NewScene();
						break;
					case StudioCommandType::OpenDocument:
						OpenScene(command.path);
						break;
					case StudioCommandType::SaveDocument:
						SaveScene();
						break;
					}
				}
				catch (std::exception const& exception) {
					auto message = std::format("Scene command failed: {}", exception.what());
					m_context->GetLogger().Write(
						fyuu_engine::LogLevel::Error,
						"Studio",
						message
					);
					m_ui->ShowSceneError(message);
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
