module;

#if !defined(ENGINE_VER_VARIANT)
#define ENGINE_VER_VARIANT 0
#endif // !defined(ENGINE_VER_VARIANT)
#if !defined(ENGINE_VER_MAJOR)
#define ENGINE_VER_MAJOR 1
#endif // !defined(ENGINE_VER_MAJOR)
#if !defined(ENGINE_VER_MINOR)
#define ENGINE_VER_MINOR 0
#endif // !defined(ENGINE_VER_MINOR)
#if !defined(ENGINE_VER_PATCH)
#define ENGINE_VER_PATCH 0
#endif // !defined(ENGINE_VER_PATCH)

#include <version>
#if !defined(__cpp_lib_modules)
#include <concepts>
#include <cstdint>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <format>
#include <optional>
#include <source_location>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#if defined(__linux__)
#include <X11/Xlib.h>
#include <wayland-client-core.h>
#include <wayland-util.h>
#endif // defined(__linux__)

module fyuu_engine:rendering_system;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :application_types;
import :log;
import :platform;
import fyuu_rhi;

namespace {

	using fyuu_engine::ApplicationDescriptor;
	using fyuu_engine::Platform;
	namespace log = fyuu_engine::log;
	constexpr auto kTriFmtPos = fyuu_rhi::ResourceFlagBits::R32G32B32A32Float;
	constexpr auto kTriFmtTarget = fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm;

		void InitializeRHILogger() noexcept {
			fyuu_rhi::log::Trace = log::Trace;
			fyuu_rhi::log::Info = log::Info;
			fyuu_rhi::log::Warning = log::Warning;
			fyuu_rhi::log::Error = log::Error;
			fyuu_rhi::log::Fatal = log::Fatal;
		}

		void LogPhysicalDevice(
			fyuu_rhi::PhysicalDeviceInfo const& info,
			std::source_location const& location = std::source_location::current()
		) {
			auto TypeName = [](fyuu_rhi::PhysicalDeviceInfo::Type type) -> std::string_view {
				using Type = fyuu_rhi::PhysicalDeviceInfo::Type;
				switch (type) {
				case Type::DiscreteGPU: return "Discrete GPU";
				case Type::IntegratedGPU: return "Integrated GPU";
				case Type::CPU: return "CPU";
				case Type::Virtual: return "Virtual";
				default: return "Unknown";
				}
			};
			auto OptionalHex = [](std::optional<std::uint32_t> value) {
				return value ? std::format("0x{:04X}", *value) : std::string("N/A");
			};
			auto MemorySize = [](std::optional<std::size_t> bytes) {
				if (!bytes) return std::string("N/A");
				return std::format("{:.2f} GiB", *bytes / (1024.0 * 1024.0 * 1024.0));
			};
			log::Info(
				std::format(
					"Physical device: {} ({}, vendor {}, device {}, dedicated memory {})",
					info.name,
					TypeName(info.type),
					OptionalHex(info.vendor_id),
					OptionalHex(info.device_id),
					MemorySize(info.dedicated_memory)
				),
				location
			);
		}

		template <class Backend>
		fyuu_rhi::Resource<Backend> SyncUpload(
			fyuu_rhi::LogicalDevice<Backend>& logical_device,
			fyuu_rhi::execution::Scheduler<Backend>& scheduler,
			fyuu_rhi::Resource<Backend>&& dest,
			std::span<std::byte const> data
		) {
			struct SyncState {
				std::mutex mtx;
				std::condition_variable cv;
				std::optional<fyuu_rhi::Resource<Backend>> result;
				std::exception_ptr error;
				bool done = false;
			} state;

			struct Recv {
				SyncState* s;
				void set_value(fyuu_rhi::Resource<Backend> r) && noexcept {
					{ std::lock_guard l(s->mtx); s->result.emplace(std::move(r)); s->done = true; }
					s->cv.notify_one();
				}
				void set_error(std::exception_ptr e) && noexcept {
					{ std::lock_guard l(s->mtx); s->error = std::move(e); s->done = true; }
					s->cv.notify_one();
				}
				void set_stopped() && noexcept {
					{ std::lock_guard l(s->mtx); s->done = true; }
					s->cv.notify_one();
				}
				[[nodiscard]] std::stop_token get_stop_token() const noexcept { return {}; }
			};

			auto sender = fyuu_rhi::execution::Upload(logical_device, scheduler, std::move(dest), 0u, data);
			auto op_state = std::move(sender).connect(Recv{ &state });
			op_state.start();
			std::unique_lock l(state.mtx);
			state.cv.wait(l, [&] { return state.done; });
			if (state.error) std::rethrow_exception(state.error);
			return std::move(*state.result);
		}

		template <class Instance, class PhysicalDevice, class LogicalDevice, class Scheduler>
		struct RenderingBackend {
			using Backend = typename Scheduler::BackendType;
			using Resource = decltype(std::declval<LogicalDevice&>().CreateTexture(
				std::size_t{},
				std::size_t{},
				std::size_t{},
				std::size_t{},
				std::declval<fyuu_rhi::ResourceFlags const&>()
			));
			using View = decltype(std::declval<LogicalDevice&>().CreateTextureView(
				std::declval<Resource const&>(),
				std::size_t{},
				std::size_t{},
				std::size_t{},
				std::size_t{},
				std::declval<fyuu_rhi::ResourceFlags const&>()
			));
			using Pipeline = decltype(std::declval<LogicalDevice&>().CreateGraphicsPipeline(
				std::declval<fyuu_rhi::pipeline::GraphicsPipelineDescriptor const&>()
			));

			struct FrameResources {
				fyuu_rhi::execution::CommandGraphDescriptor descriptor;
				Resource target;
				View view;
				fyuu_rhi::execution::CommandGraph<Backend> graph;
				fyuu_rhi::execution::ExecutableGraph<Backend> executable;
				fyuu_rhi::execution::CommandGraphBindings<Backend> bindings;
				std::uint32_t width;
				std::uint32_t height;
				Pipeline triangle_pipeline;
				fyuu_rhi::Resource<Backend> vertex_buffer;

				static fyuu_rhi::ResourceFlags TargetFlags() {
					fyuu_rhi::ResourceFlags flags;
					flags.Set(fyuu_rhi::ResourceFlagBits::DeviceLocal);
					flags.Set(fyuu_rhi::ResourceFlagBits::Texture2D);
					flags.Set(fyuu_rhi::ResourceFlagBits::RenderAttachment);
					flags.Set(fyuu_rhi::ResourceFlagBits::CopySRC);
					flags.Set(fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm);
					return flags;
				}

				static fyuu_rhi::execution::CommandGraphDescriptor BuildDescriptor(
					std::uint32_t width,
					std::uint32_t height,
					bool vertical_sync,
					std::uint32_t frames_in_flight
				) {
					using namespace fyuu_rhi::execution;
					CommandGraphBuilder builder;
					auto resource = builder.RegisterResource();
					auto view = builder.RegisterView();
					auto presentation = builder.RegisterPresentationTarget();
					auto vertex_buf = builder.RegisterResource();
					auto pipeline_id = builder.RegisterPipeline();
					auto render = builder.AddNode(GraphNodeFlagBits::Graphics);
					builder.AddAccess(render, {
						.resource = resource,
						.flags = GraphAccessFlagBits::Write |
							GraphAccessFlagBits::ColorAttachment
					});
					builder.AddAccess(render, {
						.resource = vertex_buf,
						.flags = GraphAccessFlagBits::Read | GraphAccessFlagBits::Vertex
					});
					builder.AddCommand(render, BeginRenderingCommand{
						.colors = {{
							.resource = resource,
							.view = view,
							.load = false,
							.store = true,
							.clear_red = 0.0f,
							.clear_green = 0.0f,
							.clear_blue = 0.0f,
							.clear_alpha = 1.0f
						}},
						.width = width,
						.height = height
					});
					builder.AddCommand(render, BindPipelineCommand{ .pipeline = pipeline_id });
					builder.AddCommand(render, BindVertexBufferCommand{
						.resource = vertex_buf, .slot = 0, .stride = 32, .offset = 0
					});
					builder.AddCommand(render, DrawCommand{ .vertex_count = 3, .instance_count = 1 });
					builder.AddCommand(render, EndRenderingCommand{});
					auto present = builder.AddNode(GraphNodeFlagBits::Present);
					builder.AddAccess(present, {
						.resource = resource,
						.flags = GraphAccessFlagBits::Read | GraphAccessFlagBits::Present
					});
					builder.AddCommand(present, PresentCommand{
						.source = resource,
						.target = presentation,
						.vertical_sync = vertical_sync,
						.frames_in_flight = frames_in_flight
					});
					return builder.Build();
				}

				FrameResources(
					LogicalDevice& logical_device,
					Scheduler& scheduler,
					typename Backend::PresentationTarget const& presentation_target,
					ApplicationDescriptor const& application
				) : descriptor(BuildDescriptor(
					application.surface_width,
					application.surface_height,
					application.vertical_sync,
					application.frames_in_flight
				)),
					target(logical_device.CreateTexture(
						application.surface_width,
						application.surface_height,
						1u,
						1u,
						TargetFlags()
					)),
					view(logical_device.CreateTextureView(
						target,
						0u,
						1u,
						0u,
						1u,
						TargetFlags()
					)),
					graph(logical_device.CreateCommandGraph(descriptor)),
					executable(logical_device.CompileCommandGraph(graph)),
					bindings(descriptor),
					width(application.surface_width),
					height(application.surface_height),
					triangle_pipeline(CreateTrianglePipeline(logical_device)),
					vertex_buffer(logical_device.CreateBuffer(
						sizeof(kTriangleVertices),
						[] {
							fyuu_rhi::ResourceFlags flags;
							flags.Set(fyuu_rhi::ResourceFlagBits::DeviceLocal);
							flags.Set(fyuu_rhi::ResourceFlagBits::VertexBuffer);
							flags.Set(fyuu_rhi::ResourceFlagBits::CopyDST);
							return flags;
						}()
					)) {
					vertex_buffer = SyncUpload(logical_device, scheduler,
						std::move(vertex_buffer),
						std::span<std::byte const>{
							reinterpret_cast<std::byte const*>(kTriangleVertices),
							sizeof(kTriangleVertices)
						}
					);
					bindings.Bind(fyuu_rhi::execution::GraphResourceID{ 0u }, target);
					bindings.Bind(fyuu_rhi::execution::GraphViewID{ 0u }, view);
					bindings.Bind(
						fyuu_rhi::execution::GraphPresentationID{ 0u },
						presentation_target
					);
					bindings.Bind(fyuu_rhi::execution::GraphResourceID{ 1u }, vertex_buffer);
					bindings.Bind(fyuu_rhi::execution::GraphPipelineID{ 0u }, triangle_pipeline);
				}

			private:
				static constexpr float kTriangleVertices[] = {
					// position            color
					 0.0f,  0.5f, 0.0f, 1.0f,   1.0f, 0.0f, 0.0f, 1.0f,
					 0.5f, -0.5f, 0.0f, 1.0f,   0.0f, 1.0f, 0.0f, 1.0f,
					-0.5f, -0.5f, 0.0f, 1.0f,   0.0f, 0.0f, 1.0f, 1.0f,
				};

				static Pipeline CreateTrianglePipeline(LogicalDevice& ld) {
					using namespace fyuu_rhi::pipeline;
					static constexpr char const kShaderSource[] = R"(
						struct VSOut { float4 pos : SV_Position; float3 color : COLOR; };
						[shader("vertex")]
						VSOut vs_main(float3 pos : POSITION, float3 color : COLOR) {
							VSOut o;
							o.pos = float4(pos, 1.0);
							o.color = color;
							return o;
						}
						[shader("fragment")]
						float4 fs_main(VSOut input) : SV_Target0 {
							return float4(input.color, 1.0);
						}
					)";
					return ld.CreateGraphicsPipeline({
						.program = {
							.modules = {{{
								SlangPipelineProgramDescriptor::Module{
									.name = "tri",
									.source = kShaderSource
								}
							}}},
							.entry_points = {{
								{.name = "vs_main", .stage = PipelineStage::Vertex},
								{.name = "fs_main", .stage = PipelineStage::Fragment}
							}}
						},
						.vertex = {
							.buffers = {{ VertexBufferLayout{ .slot = 0, .stride = 32 } }},
							.attributes = {{
								{.location = 0, .slot = 0, .offset = 0,
									.format = kTriFmtPos},
								{.location = 1, .slot = 0, .offset = 16,
									.format = kTriFmtPos}
							}}
						},
						.color_targets = {{ ColorTargetState{ .format = kTriFmtTarget } }}
					});
				}
			};

			struct Completion {
				std::mutex mutex;
				std::condition_variable condition;
				std::exception_ptr error;
				bool completed = false;
				bool stopped = false;
			};

			struct Receiver {
				Completion* completion;

				void set_value() && noexcept {
					{
						std::unique_lock<std::mutex> lock(completion->mutex);
						completion->completed = true;
					}
					completion->condition.notify_one();
				}

				void set_error(std::exception_ptr const& error) && noexcept {
					{
						std::unique_lock<std::mutex> lock(completion->mutex);
						completion->error = error;
						completion->completed = true;
					}
					completion->condition.notify_one();
				}

				void set_stopped() && noexcept {
					{
						std::unique_lock<std::mutex> lock(completion->mutex);
						completion->stopped = true;
						completion->completed = true;
					}
					completion->condition.notify_one();
				}
			};

			Instance& instance;
			PhysicalDevice physical_device;
			LogicalDevice logical_device;
			Scheduler scheduler;
			std::optional<FrameResources> frame;

			static PhysicalDevice SelectPhysicalDevice(Instance const& instance) {
				auto physical_devices = instance.EnumeratePhysicalDevices();
				return fyuu_rhi::BestPerformance(physical_devices);
			}

			static Scheduler CreateUnifiedScheduler(LogicalDevice& logical_device) {
				fyuu_rhi::execution::SchedulerDescriptor descriptor;
				descriptor.flags.Set(fyuu_rhi::execution::SchedulerFlagBits::Graphics);
				descriptor.flags.Set(fyuu_rhi::execution::SchedulerFlagBits::Compute);
				descriptor.flags.Set(fyuu_rhi::execution::SchedulerFlagBits::Copy);
				return logical_device.CreateScheduler(descriptor);
			}

			explicit RenderingBackend(Instance& instance_)
				: instance(instance_),
				physical_device(SelectPhysicalDevice(instance_)),
				logical_device(physical_device.CreateLogicalDevice()),
				scheduler(CreateUnifiedScheduler(logical_device)) {
				LogPhysicalDevice(physical_device.GetInfo());
			}

			void Render(
				typename Backend::PresentationTarget const& presentation_target,
				ApplicationDescriptor const& application
			) {
				if (!frame || frame->width != application.surface_width ||
					frame->height != application.surface_height) {
					frame.emplace(logical_device, scheduler, presentation_target, application);
				}
				Completion completion;
				auto sender = fyuu_rhi::execution::Submit(
					scheduler,
					frame->executable,
					frame->bindings
				);
				auto operation = sender.connect(Receiver{ &completion });
				operation.start();
				{
					std::unique_lock<std::mutex> lock(completion.mutex);
					completion.condition.wait(lock, [&completion]() noexcept {
						return completion.completed;
					});
				}
				if (completion.error) {
					std::rethrow_exception(completion.error);
				}
				if (completion.stopped) {
					throw std::runtime_error("Rendering submission was cancelled");
				}
			}
		};

		fyuu_rhi::Version RHIApplicationVersion(ApplicationDescriptor const& application) noexcept {
			return {
				.variant = application.version.variant,
				.major = application.version.major,
				.minor = application.version.minor,
				.patch = application.version.patch
			};
		}

		constexpr fyuu_rhi::Version EngineVersion{
			.variant = ENGINE_VER_VARIANT,
			.major = ENGINE_VER_MAJOR,
			.minor = ENGINE_VER_MINOR,
			.patch = ENGINE_VER_PATCH
		};

#if defined(__linux__)
		class NotX11 : public std::runtime_error {
		public:
			NotX11() : std::runtime_error("SDL window is not using X11") {

			}
		};

		std::pair<Display*, Window> X11Window(SDL_Window* window) {
			auto properties = SDL_GetWindowProperties(window);
			if (!properties) {
				throw std::runtime_error(std::format(
					"Calling SDL_GetWindowProperties(), SDL reports {}",
					SDL_GetError()
				));
			}
			auto display = static_cast<Display*>(SDL_GetPointerProperty(
				properties,
				SDL_PROP_WINDOW_X11_DISPLAY_POINTER,
				nullptr
			));
			if (!display) {
				throw NotX11{};
			}
			auto window_id = SDL_GetNumberProperty(
				properties,
				SDL_PROP_WINDOW_X11_WINDOW_NUMBER,
				0
			);
			if (window_id == 0) {
				throw std::runtime_error("SDL did not provide an X11 window handle");
			}
			return { display, static_cast<Window>(window_id) };
		}

		std::pair<wl_display*, wl_surface*> WaylandWindow(SDL_Window* window) {
			auto properties = SDL_GetWindowProperties(window);
			if (!properties) {
				throw std::runtime_error(std::format(
					"Calling SDL_GetWindowProperties(), SDL reports {}",
					SDL_GetError()
				));
			}
			auto display = static_cast<wl_display*>(SDL_GetPointerProperty(
				properties,
				SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER,
				nullptr
			));
			auto surface = static_cast<wl_surface*>(SDL_GetPointerProperty(
				properties,
				SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER,
				nullptr
			));
			if (!display || !surface) {
				throw std::runtime_error("SDL did not provide Wayland display and surface handles");
			}
			return { display, surface };
		}
#endif // defined(__linux__)

		template <class Backend>
		typename Backend::PresentationTarget PresentationTarget(Platform const& platform) {
#if defined(_WIN32)
			return reinterpret_cast<typename Backend::PresentationTarget>(
				platform.NativeWindow()
			);
#elif defined(__linux__)
			try {
				auto [display, window] = X11Window(platform.MainWindow());
				return typename Backend::X11PresentationTarget{ display, window };
			}
			catch (NotX11 const&) {
				auto [display, surface] = WaylandWindow(platform.MainWindow());
				return typename Backend::WaylandPresentationTarget{ display, surface };
			}
#else
			throw std::runtime_error("Presentation target is not connected on this platform");
#endif // defined(_WIN32)
		}

	}

namespace fyuu_engine {

	class RenderingSystem {
	private:
#if defined(_WIN32)
		using D3D12Backend = RenderingBackend<
			fyuu_rhi::D3D12Instance,
			fyuu_rhi::D3D12PhysicalDevice,
			fyuu_rhi::D3D12LogicalDevice,
			fyuu_rhi::execution::D3D12Scheduler
		>;
#endif // defined(_WIN32)
#if !defined(__APPLE__)
		using VulkanBackend = RenderingBackend<
			fyuu_rhi::VulkanInstance,
			fyuu_rhi::VulkanPhysicalDevice,
			fyuu_rhi::VulkanLogicalDevice,
			fyuu_rhi::execution::VulkanScheduler
		>;
		using OpenGLBackend = RenderingBackend<
			fyuu_rhi::OpenGLInstance,
			fyuu_rhi::OpenGLPhysicalDevice,
			fyuu_rhi::OpenGLLogicalDevice,
			fyuu_rhi::execution::OpenGLScheduler
		>;
#endif // !defined(__APPLE__)
		using WebGPUBackend = RenderingBackend<
			fyuu_rhi::WebGPUInstance,
			fyuu_rhi::WebGPUPhysicalDevice,
			fyuu_rhi::WebGPULogicalDevice,
			fyuu_rhi::execution::WebGPUScheduler
		>;

		using Backend = std::variant<
			std::monostate,
#if defined(_WIN32)
			D3D12Backend,
#endif // defined(_WIN32)
#if !defined(__APPLE__)
			VulkanBackend,
			OpenGLBackend,
#endif // !defined(__APPLE__)
			WebGPUBackend
		>;

		Backend m_backend;

		void InitializeD3D12(ApplicationDescriptor const& application) {
#if defined(_WIN32)
			auto version = RHIApplicationVersion(application);
			fyuu_rhi::D3D12Instance::Initialize(
				application.name,
				version,
				"FyuuEngine",
				EngineVersion
			);
			m_backend.emplace<D3D12Backend>(*fyuu_rhi::D3D12Instance::Get());
#else
			throw std::invalid_argument("D3D12 is unavailable on this platform");
#endif // defined(_WIN32)
		}

		void InitializeVulkan(ApplicationDescriptor const& application) {
#if !defined(__APPLE__)
			auto version = RHIApplicationVersion(application);
			fyuu_rhi::VulkanInstance::Initialize(
				application.name,
				version,
				"FyuuEngine",
				EngineVersion
			);
			m_backend.emplace<VulkanBackend>(*fyuu_rhi::VulkanInstance::Get());
#else
			throw std::invalid_argument("Vulkan is unavailable on this platform");
#endif // !defined(__APPLE__)
		}

		void InitializeOpenGL(
			Platform const& platform,
			ApplicationDescriptor const& application
		) {
#if defined(_WIN32)
			auto version = RHIApplicationVersion(application);
			fyuu_rhi::OpenGLInstance::Initialize(
				application.name,
				version,
				"FyuuEngine",
				EngineVersion,
				PresentationTarget<OpenGLBackend::Backend>(platform)
			);
			m_backend.emplace<OpenGLBackend>(*fyuu_rhi::OpenGLInstance::Get());
#elif defined(__linux__)
			auto version = RHIApplicationVersion(application);
			try {
				auto [display, window] = X11Window(platform.MainWindow());
				fyuu_rhi::OpenGLInstance::Initialize(
					application.name,
					version,
					"FyuuEngine",
					EngineVersion,
					display,
					window
				);
			}
			catch (NotX11 const&) {
				auto [display, surface] = WaylandWindow(platform.MainWindow());
				fyuu_rhi::OpenGLInstance::Initialize(
					application.name,
					version,
					"FyuuEngine",
					EngineVersion,
					display,
					surface,
					application.surface_width,
					application.surface_height
				);
			}
			m_backend.emplace<OpenGLBackend>(*fyuu_rhi::OpenGLInstance::Get());
#else
			throw std::runtime_error("OpenGL Android platform context is not connected yet");
#endif // defined(_WIN32)
		}

		void InitializeWebGPU(ApplicationDescriptor const& application) {
#if defined(__ANDROID__)
			throw std::runtime_error("WebGPU Android application state is not connected yet");
#else
			auto version = RHIApplicationVersion(application);
			fyuu_rhi::WebGPUInstance::Initialize(
				application.name,
				version,
				"FyuuEngine",
				EngineVersion
			);
			m_backend.emplace<WebGPUBackend>(*fyuu_rhi::WebGPUInstance::Get());
#endif // defined(__ANDROID__)
		}

	public:
		RenderingSystem() noexcept = default;
		RenderingSystem(RenderingSystem const&) = delete;
		RenderingSystem& operator=(RenderingSystem const&) = delete;
		RenderingSystem(RenderingSystem&&) = delete;
		RenderingSystem& operator=(RenderingSystem&&) = delete;
		~RenderingSystem() noexcept = default;

		void Initialize(
			Platform const& platform,
			ApplicationDescriptor const& application
		) {
			if (!std::holds_alternative<std::monostate>(m_backend)) {
				throw std::logic_error("RenderingSystem can only be initialized once");
			}
			InitializeRHILogger();
			auto const& api = application.graphics_api;
			if (api == "platformdefault") {
#if defined(_WIN32)
				InitializeD3D12(application);
#elif defined(__APPLE__)
				throw std::runtime_error("Metal RHI backend is not implemented");
#else
				InitializeVulkan(application);
#endif // defined(_WIN32)
			}
			else if (api == "d3d12") {
				InitializeD3D12(application);
			}
			else if (api == "vulkan") {
				InitializeVulkan(application);
			}
			else if (api == "opengl") {
				InitializeOpenGL(platform, application);
			}
			else if (api == "webgpu") {
				InitializeWebGPU(application);
			}
			else {
				throw std::invalid_argument("Unknown graphics API: " + api);
			}
		}

		void Shutdown() noexcept {
			m_backend.emplace<std::monostate>();
		}

		void Render(
			Platform const& platform,
			ApplicationDescriptor const& application
		) {
			auto RenderBackend = [&platform, &application](auto& backend) {
				using State = std::remove_cvref_t<decltype(backend)>;
				if constexpr (!std::same_as<State, std::monostate>) {
					backend.Render(
						PresentationTarget<typename State::Backend>(platform),
						application
					);
				}
			};
			std::visit(RenderBackend, m_backend);
		}

		[[nodiscard]] bool Initialized() const noexcept {
			return !m_backend.valueless_by_exception() &&
				!std::holds_alternative<std::monostate>(m_backend);
		}
	};

}
