#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

#include <vector>

#include <algorithm>

#include <iostream>

#include <cstdint>

#include <array>

#include <chrono>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <optional>

#include <string_view>

#include <source_location>

#include <span>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#include <X11/Xlib.h>
#elif defined(__APPLE__)
#include <QuartzCore/CAMetalLayer.hpp>
#endif // defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_rhi;

namespace {

	using namespace std::chrono_literals;
	constexpr std::uint32_t TargetWidth = 640u;
	constexpr std::uint32_t TargetHeight = 480u;

#if defined(_WIN32)
	class TestWindow {
	private:
		HWND m_impl;

		static LRESULT CALLBACK WindowProcedure(
			HWND window,
			UINT message,
			WPARAM word_parameter,
			LPARAM long_parameter
		) noexcept {
			if (message == WM_CLOSE) {
				DestroyWindow(window);
				return 0;
			}
			if (message == WM_DESTROY) {
				PostQuitMessage(0);
				return 0;
			}
			return DefWindowProc(
				window,
				message,
				word_parameter,
				long_parameter
			);
		}

		static ATOM RegisterWindowClass() {
			static constexpr TCHAR ClassName[] = TEXT("FyuuRHI.HelloTriangle");
			static auto const atom = []() {
				WNDCLASS description{
					.style = CS_OWNDC,
					.lpfnWndProc = WindowProcedure,
					.hInstance = GetModuleHandle(nullptr),
					.hCursor = LoadCursor(nullptr, IDC_ARROW),
					.lpszClassName = ClassName
				};
				return RegisterClass(&description);
			}();
			return atom;
		}

	public:
		TestWindow()
			: m_impl(nullptr) {
			if (!RegisterWindowClass()) {
				throw std::runtime_error("Failed to register the Hello Triangle window class");
			}
			RECT rectangle{
				0,
				0,
				static_cast<LONG>(TargetWidth),
				static_cast<LONG>(TargetHeight)
			};
			if (!AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE)) {
				throw std::runtime_error("Failed to calculate the Hello Triangle window size");
			}
			m_impl = CreateWindow(
				TEXT("FyuuRHI.HelloTriangle"),
				TEXT("FyuuRHI Hello Triangle"),
				WS_OVERLAPPEDWINDOW | WS_VISIBLE,
				CW_USEDEFAULT,
				CW_USEDEFAULT,
				rectangle.right - rectangle.left,
				rectangle.bottom - rectangle.top,
				nullptr,
				nullptr,
				GetModuleHandle(nullptr),
				nullptr
			);
			if (!m_impl) {
				throw std::runtime_error("Failed to create the Hello Triangle window");
			}
		}

		TestWindow(TestWindow const&) = delete;
		TestWindow& operator=(TestWindow const&) = delete;

		~TestWindow() noexcept {
			if (m_impl) {
				DestroyWindow(m_impl);
			}
		}

		HWND Handle() const noexcept {
			return m_impl;
		}

		bool PumpMessages() {
			MSG message;
			while (PeekMessage(&message, nullptr, 0u, 0u, PM_REMOVE)) {
				if (message.message == WM_QUIT) {
					m_impl = nullptr;
					return false;
				}
				TranslateMessage(&message);
				DispatchMessage(&message);
			}
			return true;
		}

		void Resize(std::uint32_t width, std::uint32_t height) {
			RECT rectangle{
				0,
				0,
				static_cast<LONG>(width),
				static_cast<LONG>(height)
			};
			if (!AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE)) {
				throw std::runtime_error("Failed to calculate the resized window extent");
			}
			if (!SetWindowPos(
				m_impl,
				nullptr,
				0,
				0,
				rectangle.right - rectangle.left,
				rectangle.bottom - rectangle.top,
				SWP_NOMOVE | SWP_NOZORDER
			)) {
				throw std::runtime_error("Failed to resize the Hello Triangle window");
			}
		}

		void Run() {
			MSG message;
			while (GetMessage(&message, nullptr, 0u, 0u) > 0) {
				TranslateMessage(&message);
				DispatchMessage(&message);
			}
			m_impl = nullptr;
		}
	};
#elif defined(__linux__) && !defined(__ANDROID__)
	class TestWindow {
	private:
		Display* m_display;
		::Window m_impl;
		Atom m_delete_message;

	public:
		TestWindow()
			: m_display(XOpenDisplay(nullptr)),
			m_impl(0u),
			m_delete_message(0u) {
			if (!m_display) {
				throw std::runtime_error("Failed to open the X11 display");
			}
			m_impl = XCreateSimpleWindow(
				m_display,
				DefaultRootWindow(m_display),
				0,
				0,
				TargetWidth,
				TargetHeight,
				0u,
				BlackPixel(m_display, DefaultScreen(m_display)),
				BlackPixel(m_display, DefaultScreen(m_display))
			);
			if (!m_impl) {
				XCloseDisplay(m_display);
				m_display = nullptr;
				throw std::runtime_error("Failed to create the X11 Hello Triangle window");
			}
			m_delete_message = XInternAtom(m_display, "WM_DELETE_WINDOW", False);
			XSetWMProtocols(m_display, m_impl, &m_delete_message, 1);
			XSelectInput(m_display, m_impl, StructureNotifyMask | ExposureMask);
			XStoreName(m_display, m_impl, "FyuuRHI Hello Triangle");
			XMapWindow(m_display, m_impl);
			XFlush(m_display);
		}

		TestWindow(TestWindow const&) = delete;
		TestWindow& operator=(TestWindow const&) = delete;

		~TestWindow() noexcept {
			if (m_impl) {
				XDestroyWindow(m_display, m_impl);
			}
			if (m_display) {
				XCloseDisplay(m_display);
			}
		}

		fyuu_rhi::execution::PlatformHandle Handle() const noexcept {
			return fyuu_rhi::execution::X11PlatformHandle{
				m_display,
				m_impl
			};
		}

		bool PumpMessages() {
			while (XPending(m_display) > 0) {
				XEvent event;
				XNextEvent(m_display, &event);
				if (
					event.type == ClientMessage &&
					static_cast<Atom>(event.xclient.data.l[0]) == m_delete_message
				) {
					return false;
				}
			}
			return true;
		}

		void Resize(std::uint32_t width, std::uint32_t height) {
			XResizeWindow(m_display, m_impl, width, height);
			XSync(m_display, False);
		}

		void Run() {
			while (PumpMessages()) {
				std::this_thread::sleep_for(1ms);
			}
		}
	};
#elif defined(__APPLE__)
	class TestWindow {
	private:
		CA::MetalLayer* m_layer;

	public:
		TestWindow()
			: m_layer(CA::MetalLayer::layer()) {
			if (!m_layer) {
				throw std::runtime_error("Failed to create the Hello Triangle Metal layer");
			}
			m_layer->setDrawableSize(
				CGSize{
					static_cast<CGFloat>(TargetWidth),
					static_cast<CGFloat>(TargetHeight)
				}
			);
		}

		CA::MetalLayer* Handle() const noexcept {
			return m_layer;
		}

		bool PumpMessages() const noexcept {
			return true;
		}

		void Resize(std::uint32_t width, std::uint32_t height) noexcept {
			m_layer->setDrawableSize(
				CGSize{
					static_cast<CGFloat>(width),
					static_cast<CGFloat>(height)
				}
			);
		}

		void Run() const noexcept {
		}
	};
#endif // defined(_WIN32)

	class TestLogSink final : public fyuu_rhi::log::Sink {
	private:
		std::atomic_bool m_has_error = false;

	public:
		void Write(
			fyuu_rhi::log::Level level,
			std::string_view message,
			std::source_location const& location
		) noexcept override {
			if (
				level == fyuu_rhi::log::Level::Error ||
				level == fyuu_rhi::log::Level::Fatal
			) {
				m_has_error.store(true, std::memory_order_relaxed);
			}
			std::clog
				<< '[' << static_cast<int>(level) << "] "
				<< location.file_name() << ':' << location.line() << ": "
				<< message << '\n';
		}

		bool HasError() const noexcept {
			return m_has_error.load(std::memory_order_relaxed);
		}
	};

	std::string_view BackendName(fyuu_rhi::Backend backend) {
		switch (backend) {
		case fyuu_rhi::Backend::Vulkan:
			return "vulkan";
		case fyuu_rhi::Backend::OpenGL:
			return "opengl";
		case fyuu_rhi::Backend::DirectX12:
			return "d3d12";
		case fyuu_rhi::Backend::Metal:
			return "metal";
		case fyuu_rhi::Backend::WebGPU:
			return "webgpu";
		default:
			throw std::invalid_argument("Unknown RHI backend");
		}
	}

	fyuu_rhi::Backend ParseBackend(std::string_view name) {
		for (auto backend : fyuu_rhi::EnumerateBackends()) {
			if (BackendName(backend) == name) {
				return backend;
			}
		}
		throw std::invalid_argument("The requested RHI backend is unavailable");
	}

	fyuu_rhi::ResourceFlags TargetFlags() {
		fyuu_rhi::ResourceFlags flags;
		flags.Set(fyuu_rhi::ResourceFlagBits::DeviceLocal);
		flags.Set(fyuu_rhi::ResourceFlagBits::Texture2D);
		flags.Set(fyuu_rhi::ResourceFlagBits::TextureView2D);
		flags.Set(fyuu_rhi::ResourceFlagBits::TextureViewAspectAll);
		flags.Set(fyuu_rhi::ResourceFlagBits::RenderAttachment);
		flags.Set(fyuu_rhi::ResourceFlagBits::CopySRC);
		flags.Set(fyuu_rhi::ResourceFlagBits::Sample1);
		flags.Set(fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm);
		return flags;
	}

	fyuu_rhi::ResourceFlags VertexBufferFlags() {
		fyuu_rhi::ResourceFlags flags;
		flags.Set(fyuu_rhi::ResourceFlagBits::HostVisible);
		flags.Set(fyuu_rhi::ResourceFlagBits::CopyDST);
		flags.Set(fyuu_rhi::ResourceFlagBits::VertexBuffer);
		return flags;
	}

	fyuu_rhi::ResourceFlags UniformBufferFlags() {
		fyuu_rhi::ResourceFlags flags;
		flags.Set(fyuu_rhi::ResourceFlagBits::HostVisible);
		flags.Set(fyuu_rhi::ResourceFlagBits::CopyDST);
		flags.Set(fyuu_rhi::ResourceFlagBits::UniformBuffer);
		return flags;
	}

	fyuu_rhi::Pipeline CreateTrianglePipeline(fyuu_rhi::LogicalDevice& device) {
		using namespace fyuu_rhi::pipeline;
		static constexpr char ShaderSource[] = R"(
			struct DynamicConstants {
				float4 color_scale;
			};
			[[vk::binding(0, 1)]]
			ConstantBuffer<DynamicConstants> dynamic_constants : register(b0, space1);

			struct PipelineConstants {
				float4 color_scale;
			};
			[[vk::push_constant]]
			ConstantBuffer<PipelineConstants> pipeline_constants;

			struct VertexOutput {
				float4 position : SV_Position;
				float4 color : COLOR0;
			};
			[shader("vertex")]
			VertexOutput vertex_main(float2 position : POSITION, float4 color : COLOR0) {
				VertexOutput output;
				output.position = float4(position, 0.0, 1.0);
				output.color = color;
				return output;
			}
			[shader("fragment")]
			float4 fragment_main(VertexOutput input) : SV_Target0 {
				return input.color * pipeline_constants.color_scale *
					dynamic_constants.color_scale;
			}
		)";
		static const std::array modules{
			SlangPipelineProgramDescriptor::Module{
				"hello_triangle_pipeline_constants",
				ShaderSource
			}
		};
		static const std::array entry_points{
			SlangPipelineProgramDescriptor::EntryPoint{ "vertex_main", Stage::Vertex },
			SlangPipelineProgramDescriptor::EntryPoint{ "fragment_main", Stage::Fragment }
		};
		static const std::array color_targets{
			ColorTargetState{ .format = fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm }
		};
		static constexpr std::array vertex_buffers{
			VertexBufferLayout{
				.slot = 0u,
				.stride = 6u * sizeof(float)
			}
		};
		static constexpr std::array vertex_attributes{
			VertexAttribute{
				.location = 0u,
				.slot = 0u,
				.offset = 0u,
				.format = fyuu_rhi::ResourceFlagBits::R32G32Float
			},
			VertexAttribute{
				.location = 1u,
				.slot = 0u,
				.offset = 2u * sizeof(float),
				.format = fyuu_rhi::ResourceFlagBits::R32G32B32A32Float
			}
		};
		return device.CreateGraphicsPipeline(
			{
				.program = {
					.modules = modules,
					.entry_points = entry_points
				},
				.vertex = {
					.buffers = vertex_buffers,
					.attributes = vertex_attributes
				},
				.color_targets = color_targets
			}
		);
	}

	struct CompletionState {
		std::mutex mutex;
		std::condition_variable condition;
		std::optional<fyuu_rhi::execution::CommandGraphResources> resources;
		std::exception_ptr error;
		bool completed = false;
		bool stopped = false;
	};

	struct CompletionReceiver {
		std::shared_ptr<CompletionState> state;

		struct Environment {
		};

		Environment get_env() const noexcept {
			return {};
		}

		void RecoverBindings(
			fyuu_rhi::execution::CommandGraphResources&& resources
		) noexcept {
			std::lock_guard lock(state->mutex);
			state->resources.emplace(std::move(resources));
		}

		void set_value(
			fyuu_rhi::execution::CommandGraphResources&& resources
		) && noexcept {
			{
				std::lock_guard lock(state->mutex);
				state->resources.emplace(std::move(resources));
				state->completed = true;
			}
			state->condition.notify_one();
		}

		void set_error(std::exception_ptr error) && noexcept {
			{
				std::lock_guard lock(state->mutex);
				state->error = std::move(error);
				state->completed = true;
			}
			state->condition.notify_one();
		}

		void set_stopped() && noexcept {
			{
				std::lock_guard lock(state->mutex);
				state->stopped = true;
				state->completed = true;
			}
			state->condition.notify_one();
		}
	};

	void TestResourceMapping(fyuu_rhi::LogicalDevice& device) {
		using namespace fyuu_rhi::execution;
		static constexpr std::array Data{
			std::byte{ 0x10 },
			std::byte{ 0x21 },
			std::byte{ 0x32 },
			std::byte{ 0x43 },
			std::byte{ 0x54 },
			std::byte{ 0x65 },
			std::byte{ 0x76 },
			std::byte{ 0x87 }
		};

		fyuu_rhi::ResourceFlags source_flags;
		source_flags.Set(fyuu_rhi::ResourceFlagBits::HostVisible);
		source_flags.Set(fyuu_rhi::ResourceFlagBits::CopySRC);
		auto source = device.CreateBuffer(Data.size(), source_flags);
		{
			auto mapping = source.Map({ 0u, Data.size() });
			mapping.Write(Data);
			mapping.Reset();
		}
		{
			auto mapping = source.Map({ 0u, Data.size() });
			mapping.Write(Data);
		}
		{
			auto mapping = source.Map({ 0u, Data.size() });
			std::array<std::byte, Data.size() + 1u> oversized{};
			bool rejected = false;
			try {
				mapping.Write(oversized);
			}
			catch (std::out_of_range const&) {
				rejected = true;
			}
			if (!rejected) {
				throw std::runtime_error("ResourceMapScope accepted an oversized write");
			}
		}

		fyuu_rhi::ResourceFlags destination_flags;
		destination_flags.Set(fyuu_rhi::ResourceFlagBits::DeviceReadback);
		destination_flags.Set(fyuu_rhi::ResourceFlagBits::CopyDST);
		auto destination = device.CreateBuffer(Data.size(), destination_flags);
		auto scheduler = device.CreateScheduler();
		auto builder = scheduler.schedule();
		auto const source_binding = builder.RegisterResource();
		auto const destination_binding = builder.RegisterResource();
		auto node = builder.CreateNode(QueueType::Transfer);
		node
		.Access(
			{
				source_binding,
				AccessMode::Read,
				ResourceUsage::CopySource,
				{}
			}
		)
		.Access(
			{
				destination_binding,
				AccessMode::Write,
				ResourceUsage::CopyDestination,
				{}
			}
		)
		.Record(
			CopyBuffer{
				.source = source_binding,
				.destination = destination_binding,
				.size = Data.size()
			}
		);

		auto completion = std::make_shared<CompletionState>();
		auto operation = std::move(builder).connect(CompletionReceiver{ completion });
		operation.BindResource(source_binding, std::move(source));
		operation.BindResource(destination_binding, std::move(destination));
		operation.start();
		{
			std::unique_lock lock(completion->mutex);
			if (!completion->condition.wait_for(
				lock,
				30s,
				[&]() {
					return completion->completed;
				}
			)) {
				throw std::runtime_error("Timed out waiting for the resource mapping copy");
			}
			if (completion->error) {
				std::rethrow_exception(completion->error);
			}
			if (completion->stopped || !completion->resources) {
				throw std::runtime_error("The resource mapping copy was stopped");
			}
			source = completion->resources->TakeResource(source_binding);
			destination = completion->resources->TakeResource(destination_binding);
		}

		{
			auto mapping = destination.Map({ 0u, Data.size() });
			if (!std::ranges::equal(mapping.Read(), Data)) {
				throw std::runtime_error("ResourceMapScope returned incorrect readback data");
			}
			bool rejected = false;
			try {
				mapping.Write(Data);
			}
			catch (std::logic_error const&) {
				rejected = true;
			}
			if (!rejected) {
				throw std::runtime_error("A readback mapping accepted a write");
			}
		}
		{
			auto mapping = destination.Map({ 0u, Data.size() });
			if (!std::ranges::equal(mapping.Read(), Data)) {
				throw std::runtime_error("A resource could not be mapped again after RAII unmap");
			}
			mapping.Reset();
		}
	}

	void ExecuteTriangle(
		fyuu_rhi::LogicalDevice& device,
		TestWindow& window,
		bool test_mode
	) {
		using namespace fyuu_rhi::execution;
		TestResourceMapping(device);
		static constexpr std::array VertexData{
			0.0f, 0.6f, 1.0f, 0.0f, 0.0f, 0.0f,
			-0.6f, -0.6f, 0.0f, 1.0f, 0.0f, 0.0f,
			0.6f, -0.6f, 0.0f, 0.0f, 1.0f, 0.0f
		};
		auto const vertex_bytes = std::as_bytes(std::span(VertexData));
		auto flags = TargetFlags();
		auto target = device.CreateTexture(
			TargetWidth,
			TargetHeight,
			1u,
			1u,
			flags
		);
		auto target_view = target.CreateTextureView(0u, 1u, 0u, 1u, flags);
		auto vertex_buffer = device.CreateBuffer(
			vertex_bytes.size(),
			VertexBufferFlags()
		);
		auto uniform_buffer = device.CreateBuffer(512u, UniformBufferFlags());
		auto pipeline = CreateTrianglePipeline(device);
		std::array resource_bindings{
			fyuu_rhi::pipeline::ResourceBinding{
				.slot = 0u,
				.value = fyuu_rhi::pipeline::BindingValue::FromBuffer(
					uniform_buffer,
					0u,
					16u
				)
			}
		};
		auto resource_group = pipeline.CreatePipelineResourceGroup(
			1u,
			resource_bindings
		);
		auto scheduler = device.CreateScheduler();
		bool running = true;
		std::size_t frame = 0u;
		do {
			std::array constant_data{
				frame == 0u ? 1.0f : 0.5f,
				1.0f,
				1.0f,
				1.0f
			};
			auto constant_bytes = std::as_bytes(std::span(constant_data));
			std::array red_override{ frame == 0u ? 0.75f : 0.25f };
			auto red_override_bytes = std::as_bytes(std::span(red_override));
			std::vector<std::byte> uniform_data(512u);
			std::array first_dynamic_color{ 1.0f, 0.5f, 1.0f, 1.0f };
			std::array second_dynamic_color{ 0.5f, 1.0f, 1.0f, 1.0f };
			std::ranges::copy(
				std::as_bytes(std::span(first_dynamic_color)),
				uniform_data.begin()
			);
			std::ranges::copy(
				std::as_bytes(std::span(second_dynamic_color)),
				uniform_data.begin() + 256u
			);
			auto builder = scheduler.schedule();
			auto const target_binding = builder.RegisterResource();
			auto const vertex_binding = builder.RegisterResource();
			auto const uniform_binding = builder.RegisterResource();
			auto const target_view_binding = builder.RegisterView();
			auto const pipeline_binding = builder.RegisterPipeline();
			auto const group_binding = builder.RegisterResourceGroup();
			auto upload = builder.CreateNode(QueueType::Transfer);
			upload.Record(
				WriteBuffer{
					.resource = vertex_binding,
					.offset = 0u,
					.data = std::vector<std::byte>(
						vertex_bytes.begin(),
						vertex_bytes.end()
					)
				}
			);
			upload.Record(
				WriteBuffer{
					.resource = uniform_binding,
					.offset = 0u,
					.data = uniform_data
				}
			);
			auto node = builder.CreateNode(QueueType::Graphics, upload);
			node
			.Access(
				{
					target_binding,
					AccessMode::Write,
					ResourceUsage::ColorAttachment,
					{}
				}
			)
			.Access(
				{
					vertex_binding,
					AccessMode::Read,
					ResourceUsage::VertexBuffer,
					{}
				}
			)
			.Access(
				{
					uniform_binding,
					AccessMode::Read,
					ResourceUsage::Uniform,
					{}
				}
			)
			.Record(BindPipeline{ pipeline_binding })
			.Record(
				BindResourceGroup{
					.group = group_binding,
					.space = 1u,
					.additional_buffer_offsets = { frame == 0u ? 0u : 256u }
				}
			)
			.Record(
				BindVertexBuffer{
					.resource = vertex_binding,
					.slot = 0u,
					.stride = 6u * sizeof(float),
					.offset = 0u
				}
			)
			.Record(
				BeginRendering{
					.area = { 0, 0, TargetWidth, TargetHeight },
					.colors = {
						{
							.resource = target_binding,
							.view = target_view_binding,
							.clear = { 0.0f, 0.0f, 0.0f, 1.0f }
						}
					}
				}
			)
			.Record(
				Viewport{
					0.0f,
					0.0f,
					static_cast<float>(TargetWidth),
					static_cast<float>(TargetHeight),
					0.0f,
					1.0f,
					ClipSpace::YUp
				}
			)
			.Record(Scissor{ 0, 0, TargetWidth, TargetHeight })
			.Record(
				SetPipelineConstants{
					.slot = 0u,
					.space = 0u,
					.offset = 0u,
					.data = std::vector<std::byte>(
						constant_bytes.begin(),
						constant_bytes.end()
					)
				}
			)
			.Record(
				SetPipelineConstants{
					.slot = 0u,
					.space = 0u,
					.offset = 0u,
					.data = std::vector<std::byte>(
						red_override_bytes.begin(),
						red_override_bytes.end()
					)
				}
			)
			.Record(Draw{ 3u, 1u, 0u, 0u })
			.Record(EndRendering{});
			auto present = builder.CreateNode(QueueType::Present, node);
			present
			.Access(
				{
					target_binding,
					AccessMode::Read,
					ResourceUsage::PresentationSource,
					{}
				}
			)
			.Record(
				Present{
					.source = target_binding,
					.target = 0u,
					.buffer_count = 3u,
					.vertical_sync = false
				}
			);

			auto completion = std::make_shared<CompletionState>();
			auto operation = std::move(builder).connect(
				CompletionReceiver{ completion }
			);
			operation.BindResource(target_binding, std::move(target));
			operation.BindResource(vertex_binding, std::move(vertex_buffer));
			operation.BindResource(uniform_binding, std::move(uniform_buffer));
			operation.BindView(target_view_binding, std::move(target_view));
			operation.BindPipeline(pipeline_binding, std::move(pipeline));
			operation.BindResourceGroup(group_binding, std::move(resource_group));
			operation.SetPresentationTarget(window.Handle());
			operation.start();

			auto const deadline = std::chrono::steady_clock::now() + 30s;
			bool completed = false;
			while (!completed) {
				{
					std::lock_guard lock(completion->mutex);
					completed = completion->completed;
				}
				if (completed) {
					break;
				}
				if (!window.PumpMessages()) {
					running = false;
				}
				if (std::chrono::steady_clock::now() >= deadline) {
					throw std::runtime_error("Timed out waiting for the triangle command graph");
				}
				std::unique_lock lock(completion->mutex);
				completion->condition.wait_for(lock, 1ms);
			}
			std::lock_guard lock(completion->mutex);
			if (completion->error) {
				std::rethrow_exception(completion->error);
			}
			if (completion->stopped) {
				throw std::runtime_error("The triangle command graph was stopped");
			}
			target = completion->resources->TakeResource(target_binding);
			vertex_buffer = completion->resources->TakeResource(vertex_binding);
			uniform_buffer = completion->resources->TakeResource(uniform_binding);
			target_view = completion->resources->TakeView(target_view_binding);
			pipeline = completion->resources->TakePipeline(pipeline_binding);
			resource_group = completion->resources->TakeResourceGroup(group_binding);
			++frame;
			if (test_mode && frame == 1u) {
				window.Resize(800u, 600u);
				(void)window.PumpMessages();
			}
		} while (running && (!test_mode || frame < 2u));
	}

	void TestBackend(
		fyuu_rhi::Backend backend,
		TestWindow& window,
		bool test_mode
	) {
		auto& instance = fyuu_rhi::RequestInstance(backend);
		auto physical_devices = instance.EnumeratePhysicalDevices();
		if (physical_devices.empty()) {
			throw std::runtime_error("The backend did not expose a physical device");
		}
		auto device = physical_devices.front().CreateLogicalDevice();
		ExecuteTriangle(device, window, test_mode);
	}

} // namespace

int main(int argument_count, char const* const* arguments) {
	try {
		if (
			argument_count < 2 ||
			argument_count > 3 ||
			(argument_count == 3 && std::string_view(arguments[2]) != "--test")
		) {
			throw std::invalid_argument("Usage: HelloTriangle <backend> [--test]");
		}
		TestLogSink sink;
		fyuu_rhi::InitializeRHIContext(
			"FyuuRHI Hello Triangle",
			{ 0u, 1u, 0u, 0u },
			"FyuuRHI",
			{ 0u, 1u, 0u, 0u },
			&sink
		);
		auto const backend = ParseBackend(arguments[1]);
		TestWindow window;
		auto const test_mode = argument_count == 3;
		TestBackend(backend, window, test_mode);
		if (sink.HasError()) {
			throw std::runtime_error(
				"The backend reported an uncaptured validation or runtime error"
			);
		}
		std::cout << "Hello triangle presented on " << BackendName(backend) << '\n';
		return 0;
	}
	catch (std::exception const& error) {
		std::cerr << "Hello triangle failed: " << error.what() << '\n';
		return 1;
	}
}
