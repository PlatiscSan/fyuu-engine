#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <utility>

#include <string>

#include <iostream>

#include <array>

#include <chrono>

#include <atomic>
#include <mutex>
#include <thread>

#include <optional>

#include <string_view>

#include <source_location>

#include <windows.h>
#include <dwmapi.h>

import fyuu_rhi;

namespace {

    constexpr unsigned Width = 320u;
    constexpr unsigned Height = 240u;

    /** @brief Pumps host events without delegating the window lifetime to RHI. */
    void Pump() {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0u, 0u, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    /** @brief Owns the single native window used throughout one backend sequence. */
    class Window {
        HWND m_handle;

    public:
        Window() : m_handle([] {
            WNDCLASSW description{};
            description.style = CS_OWNDC;
            description.lpfnWndProc = DefWindowProcW;
            description.hInstance = GetModuleHandleW(nullptr);
            description.lpszClassName = L"FyuuRHIWindowReuse";
            if (!RegisterClassW(&description)) {
                throw std::runtime_error("RegisterClassW failed");
            }
            RECT rectangle{0, 0, Width, Height};
            AdjustWindowRectEx(&rectangle, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_TOPMOST);
            auto handle = CreateWindowExW(WS_EX_TOPMOST, description.lpszClassName,
                L"RHI window reuse test", WS_OVERLAPPEDWINDOW, 80, 80,
                rectangle.right - rectangle.left, rectangle.bottom - rectangle.top,
                nullptr, nullptr, description.hInstance, nullptr);
            if (!handle) {
                throw std::runtime_error("CreateWindowExW failed");
            }
            ShowWindow(handle, SW_SHOWNOACTIVATE);
            UpdateWindow(handle);
            return handle;
        }()) {
        }

        Window(Window const&) = delete;
        Window& operator=(Window const&) = delete;

        ~Window() {
            DestroyWindow(m_handle);
            UnregisterClassW(L"FyuuRHIWindowReuse", GetModuleHandleW(nullptr));
        }

        HWND Handle() const noexcept {
            return m_handle;
        }
    };

    /** @brief Serializes diagnostics emitted by backend workers. */
    class Sink final : public fyuu_rhi::log::Sink {
        std::mutex m_mutex;

    public:
        void Write(fyuu_rhi::log::Level level, std::string_view message,
            std::source_location const&) noexcept override {
            std::lock_guard lock(m_mutex);
            std::cerr << "RHI " << static_cast<int>(level) << ' ' << message << '\n';
        }
    };

    /** @brief Stores one graph's returned ownership until the host observes completion. */
    struct Frame {
        std::optional<fyuu_rhi::execution::CommandGraphResources> resources;
        std::exception_ptr error;
        bool stopped = false;
        std::atomic_bool done = false;
    };

    /** @brief Borrows a frame until its terminal callback has been drained. */
    struct Receiver {
        Frame* frame;

        struct Environment {};
        Environment get_env() const noexcept {
            return {};
        }
        void RecoverBindings(fyuu_rhi::execution::CommandGraphResources&& resources) noexcept {
            frame->resources.emplace(std::move(resources));
        }
        void set_value(fyuu_rhi::execution::CommandGraphResources&& resources) && noexcept {
            frame->resources.emplace(std::move(resources));
            frame->done.store(true, std::memory_order_release);
        }
        void set_error(std::exception_ptr error) && noexcept {
            frame->error = error;
            frame->done.store(true, std::memory_order_release);
        }
        void set_stopped() && noexcept {
            frame->stopped = true;
            frame->done.store(true, std::memory_order_release);
        }
    };

    fyuu_rhi::Backend ParseBackend(std::string_view name) {
        if (name == "d3d12") {
            return fyuu_rhi::Backend::DirectX12;
        }
        if (name == "vulkan") {
            return fyuu_rhi::Backend::Vulkan;
        }
        if (name == "opengl") {
            return fyuu_rhi::Backend::OpenGL;
        }
        if (name == "webgpu") {
            return fyuu_rhi::Backend::WebGPU;
        }
        throw std::invalid_argument("Unknown backend");
    }

    /** @brief Clears and presents without shaders, isolating native window handoff. */
    void PresentFrame(fyuu_rhi::LogicalDevice& device,
        fyuu_rhi::execution::CommandScheduler const& scheduler, HWND window,
        fyuu_rhi::ColorClearValue const& color, bool vertical_sync, bool srgb) {
        using namespace fyuu_rhi;
        using namespace fyuu_rhi::execution;
        ResourceFlags flags;
        for (auto bit : {ResourceFlagBits::DeviceLocal, ResourceFlagBits::Texture2D,
            ResourceFlagBits::TextureView2D, ResourceFlagBits::TextureViewAspectAll,
            ResourceFlagBits::RenderAttachment, ResourceFlagBits::CopySRC,
            ResourceFlagBits::Sample1,
            srgb ? ResourceFlagBits::R8G8B8A8Srgb : ResourceFlagBits::R8G8B8A8Unorm}) {
            flags.Set(bit);
        }
        auto texture = device.CreateTexture(Width, Height, 1u, 1u, flags);
        auto view = texture.CreateTextureView(0u, 1u, 0u, 1u, flags);
        auto builder = scheduler.schedule();
        auto resource_slot = builder.RegisterResource();
        auto view_slot = builder.RegisterView();
        auto render = builder.CreateNode(QueueType::Graphics);
        render.Access({resource_slot, AccessMode::Write, ResourceUsage::ColorAttachment, {}})
            .Record(BeginRendering{.area = {0, 0, Width, Height},
                .colors = {{.resource = resource_slot, .view = view_slot, .clear = color,
                    .resolve_resource = {}, .resolve_view = {}}}, .depth_stencil = {}})
            .Record(EndRendering{});
        builder.CreateNode(QueueType::Present, render)
            .Access({resource_slot, AccessMode::Read, ResourceUsage::PresentationSource, {}})
            .Record(Present{.source = resource_slot, .target = 0u,
                .buffer_count = 3u, .vertical_sync = vertical_sync});
        Frame frame;
        auto operation = std::move(builder).connect(Receiver{&frame});
        operation.BindResource(resource_slot, std::move(texture));
        operation.BindView(view_slot, std::move(view));
        operation.SetPresentationTarget(window);
        operation.start();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!frame.done.load(std::memory_order_acquire)) {
            Pump();
            if (std::chrono::steady_clock::now() > deadline) {
                std::cerr << "FAIL completion timeout\n" << std::flush;
                std::_Exit(3);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (frame.error) {
            std::rethrow_exception(frame.error);
        }
        if (frame.stopped) {
            throw std::runtime_error("Graph stopped");
        }
    }

    /** @brief Samples five desktop pixels to check actual visible presentation. */
    bool CheckPixels(HWND window, COLORREF expected, int tolerance) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        while (std::chrono::steady_clock::now() < deadline) {
            Pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        DwmFlush();
        auto dc = GetDC(nullptr);
        if (!dc) {
            throw std::runtime_error("Desktop GetDC failed");
        }
        bool matched = true;
        for (POINT point : {POINT{80, 60}, POINT{240, 60}, POINT{160, 120},
            POINT{80, 180}, POINT{240, 180}}) {
            ClientToScreen(window, &point);
            auto actual = GetPixel(dc, point.x, point.y);
            std::cout << "PIXEL " << point.x << ',' << point.y << " actual="
                << +GetRValue(actual) << ',' << +GetGValue(actual) << ',' << +GetBValue(actual)
                << " expected=" << +GetRValue(expected) << ',' << +GetGValue(expected)
                << ',' << +GetBValue(expected) << '\n';
            if (actual == CLR_INVALID ||
                std::abs(int(GetRValue(actual)) - int(GetRValue(expected))) > tolerance ||
                std::abs(int(GetGValue(actual)) - int(GetGValue(expected))) > tolerance ||
                std::abs(int(GetBValue(actual)) - int(GetBValue(expected))) > tolerance) {
                matched = false;
            }
        }
        ReleaseDC(nullptr, dc);
        return matched;
    }
}

int main(int argc, char** argv) {
    Sink sink;
    bool failed = false;
    try {
        bool fresh_window = argc > 1 && std::string_view(argv[1]) == "--fresh";
        int first_phase = fresh_window ? 2 : 1;
        bool srgb = argc > first_phase && std::string_view(argv[first_phase]) == "--srgb";
        if (srgb) {
            ++first_phase;
        }
        if (argc <= first_phase) {
            throw std::invalid_argument("Pass one or more backend names");
        }
        fyuu_rhi::InitializeRHIContext("WindowReuse", {0, 1, 0, 0},
            "FyuuRHI", {0, 1, 0, 0}, &sink);
        std::optional<Window> window{std::in_place};
        std::cout << "HWND " << window->Handle() << '\n';
        std::array colors{fyuu_rhi::ColorClearValue{1, 0, 0, 1},
            fyuu_rhi::ColorClearValue{0, 1, 0, 1}, fyuu_rhi::ColorClearValue{0, 0, 1, 1}};
        std::array expected{RGB(255, 0, 0), RGB(0, 255, 0), RGB(0, 0, 255)};
        if (srgb) {
            colors = {fyuu_rhi::ColorClearValue{0.25f, 0.5f, 0.75f, 1},
                fyuu_rhi::ColorClearValue{0.75f, 0.25f, 0.5f, 1},
                fyuu_rhi::ColorClearValue{0.5f, 0.75f, 0.25f, 1}};
            expected = {RGB(137, 188, 225), RGB(225, 137, 188), RGB(188, 225, 137)};
        }
        for (int phase = first_phase; phase < argc; ++phase) {
            if (fresh_window && phase != first_phase) {
                window.emplace();
            }
            auto dc = GetDC(window->Handle());
            auto pixel_format = GetPixelFormat(dc);
            ReleaseDC(window->Handle(), dc);
            std::cout << "BEGIN " << phase - first_phase + 1 << ' ' << argv[phase] << " hwnd="
                << window->Handle() << " pixel_format=" << pixel_format << '\n' << std::flush;
            try {
                {
                    auto& instance = fyuu_rhi::RequestInstance(ParseBackend(argv[phase]));
                    auto adapters = instance.EnumeratePhysicalDevices();
                    auto const& adapter = fyuu_rhi::BestPerformance(adapters);
                    std::cout << "ADAPTER " << adapter.GetInfo().name << '\n';
                    auto device = adapter.CreateLogicalDevice();
                    auto scheduler = device.CreateScheduler();
                    for (int frame = 0; frame < 12; ++frame) {
                        Pump();
                        PresentFrame(device, scheduler, window->Handle(), colors[(phase - first_phase) % 3],
                            std::string_view(argv[phase]) != "webgpu" || (frame / 3) % 2 != 0, srgb);
                    }
                    auto pixels = CheckPixels(window->Handle(), expected[(phase - first_phase) % 3], srgb ? 1 : 0);
                    std::cout << (pixels ? "PASS " : "FAIL pixels ") << argv[phase] << '\n';
                    failed = failed || !pixels;
                    fyuu_rhi::execution::ShutdownCompletionPool();
                }
                std::cout << "RELEASED " << argv[phase] << '\n' << std::flush;
            } catch (std::exception const& error) {
                fyuu_rhi::execution::ShutdownCompletionPool();
                std::cout << "FAIL " << argv[phase] << ' ' << error.what() << '\n' << std::flush;
                failed = true;
                break;
            }
        }
        fyuu_rhi::RebindRHISink(nullptr);
    } catch (std::exception const& error) {
        std::cerr << "FAIL setup " << error.what() << '\n';
        failed = true;
    }
    fyuu_rhi::execution::ShutdownCompletionPool();
    return failed ? 1 : 0;
}
