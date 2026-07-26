#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
import fyuu_rhi;

namespace {
	constexpr std::uint32_t RowPitch = 256u;
	constexpr auto CompletionTimeout = std::chrono::seconds(30u);

	struct TextureTransferCase {
		std::string_view name;
		std::uint32_t texture_width;
		std::uint32_t texture_height;
		std::uint32_t depth_or_array_layers;
		std::uint32_t mip_levels;
		std::uint32_t rows_per_image;
		bool texture_3d = false;
		fyuu_rhi::TextureRegion region;
	};

	constexpr TextureTransferCase TextureTransferCases[]{
		{ "basic", 4u, 4u, 1u, 1u, 4u, false, { .width = 4u, .height = 4u } },
		{ "non-tight rows", 9u, 5u, 1u, 1u, 5u, false, { .width = 9u, .height = 5u } },
		{ "mip level", 16u, 8u, 1u, 4u, 2u, false,
			{ .mip_level = 2u, .width = 4u, .height = 2u } },
		{ "array layers", 4u, 3u, 4u, 1u, 4u, false,
			{
				.base_array_layer = 1u,
				.array_layer_count = 2u,
				.width = 4u,
				.height = 3u
			} },
		{ "3D slices", 4u, 3u, 4u, 1u, 4u, true,
			{
				.offset_z = 1u,
				.width = 4u,
				.height = 3u,
				.depth = 2u
			} }
	};

	template <class Resource> struct UploadState {
		std::mutex mutex;
		std::condition_variable condition;
		std::optional<Resource> resource;
		std::exception_ptr error;
		bool stopped = false;
		bool completed = false;
	};

	template <class Resource> struct UploadReceiver {
		std::shared_ptr<UploadState<Resource>> state;

		void set_value(Resource resource) && noexcept {
			{
				std::unique_lock<std::mutex> lock(state->mutex);
				state->resource.emplace(std::move(resource));
				state->completed = true;
			}
			state->condition.notify_one();
		}

		void set_error(std::exception_ptr const& error) && noexcept {
			{
				std::unique_lock<std::mutex> lock(state->mutex);
				state->error = error;
				state->completed = true;
			}
			state->condition.notify_one();
		}

		void set_stopped() && noexcept {
			{
				std::unique_lock<std::mutex> lock(state->mutex);
				state->stopped = true;
				state->completed = true;
			}
			state->condition.notify_one();
		}
	};

	template <class Resource> struct ReadbackState {
		std::mutex mutex;
		std::condition_variable condition;
		std::optional<Resource> resource;
		std::vector<std::byte> data;
		std::exception_ptr error;
		bool stopped = false;
		bool completed = false;
	};

	template <class Resource> struct ReadbackReceiver {
		std::shared_ptr<ReadbackState<Resource>> state;

		void set_value(Resource resource, std::vector<std::byte> data) && noexcept {
			{
				std::unique_lock<std::mutex> lock(state->mutex);
				state->resource.emplace(std::move(resource));
				state->data = std::move(data);
				state->completed = true;
			}
			state->condition.notify_one();
		}

		void set_error(std::exception_ptr const& error) && noexcept {
			{
				std::unique_lock<std::mutex> lock(state->mutex);
				state->error = error;
				state->completed = true;
			}
			state->condition.notify_one();
		}

		void set_stopped() && noexcept {
			{
				std::unique_lock<std::mutex> lock(state->mutex);
				state->stopped = true;
				state->completed = true;
			}
			state->condition.notify_one();
		}
	};

	template <class State>
	void Wait(State& state, std::string_view operation) {
		std::unique_lock<std::mutex> lock(state.mutex);
		auto Completed = [&state]() noexcept { return state.completed; };
		if (!state.condition.wait_for(lock, CompletionTimeout, Completed)) {
			throw std::runtime_error(std::string(operation) + " timed out");
		}
		if (state.error) {
			std::rethrow_exception(state.error);
		}
		if (state.stopped) {
			throw std::runtime_error(std::string(operation) + " was stopped");
		}
	}

	fyuu_rhi::ResourceFlags TextureFlags(bool texture_3d) {
		fyuu_rhi::ResourceFlags flags;
		flags.Set(fyuu_rhi::ResourceFlagBits::DeviceLocal);
		flags.Set(fyuu_rhi::ResourceFlagBits::CopySRC);
		flags.Set(fyuu_rhi::ResourceFlagBits::CopyDST);
		flags.Set(fyuu_rhi::ResourceFlagBits::R8G8B8A8Unorm);
		flags.Set(
			texture_3d
				? fyuu_rhi::ResourceFlagBits::Texture3D
				: fyuu_rhi::ResourceFlagBits::Texture2D
		);
		return flags;
	}

	fyuu_rhi::execution::SchedulerDescriptor CopySchedulerDescriptor() {
		fyuu_rhi::execution::SchedulerDescriptor descriptor;
		descriptor.flags.Set(fyuu_rhi::execution::SchedulerFlagBits::Copy);
		return descriptor;
	}

	std::vector<std::byte> TestPixels(TextureTransferCase const& test_case) {
		auto images = static_cast<std::size_t>(test_case.region.depth) *
			test_case.region.array_layer_count;
		auto rows = static_cast<std::size_t>(test_case.rows_per_image) * (images - 1u) +
			test_case.region.height;
		std::vector<std::byte> result(RowPitch * rows);
		for (std::size_t image = 0u; image < images; ++image) {
			for (std::uint32_t row = 0u; row < test_case.region.height; ++row) {
				for (std::uint32_t column = 0u; column < test_case.region.width; ++column) {
					auto offset = (image * test_case.rows_per_image + row) * RowPitch + column * 4u;
					result[offset] = static_cast<std::byte>(image * 37u + row * 11u + column);
					result[offset + 1u] = static_cast<std::byte>(0x40u + image + row);
					result[offset + 2u] = static_cast<std::byte>(0x80u + column);
					result[offset + 3u] = std::byte{ 0xffu };
				}
			}
		}
		return result;
	}

	template <class LogicalDevice, class Scheduler>
	void RunTextureTransfer(
		LogicalDevice& logical_device,
		Scheduler const& scheduler,
		TextureTransferCase const& test_case
	) {
		auto texture = logical_device.CreateTexture(
			test_case.texture_width,
			test_case.texture_height,
			test_case.depth_or_array_layers,
			test_case.mip_levels,
			TextureFlags(test_case.texture_3d)
		);
		using Resource = decltype(texture);
		fyuu_rhi::TextureDataLayout layout{
			.bytes_per_row = RowPitch,
			.rows_per_image = test_case.rows_per_image
		};
		auto expected = TestPixels(test_case);

		auto upload_state = std::make_shared<UploadState<Resource>>();
		auto upload_sender = fyuu_rhi::execution::Upload(
			logical_device,
			scheduler,
			std::move(texture),
			layout,
			test_case.region,
			expected
		);
		auto upload_operation = std::move(upload_sender).connect(
			UploadReceiver<Resource>{ upload_state }
		);
		upload_operation.start();
		Wait(*upload_state, std::string(test_case.name) + " texture upload");

		auto uploaded_texture = std::move(*upload_state->resource);
		upload_state->resource.reset();
		auto readback_state = std::make_shared<ReadbackState<Resource>>();
		auto readback_sender = fyuu_rhi::execution::Readback(
			logical_device,
			scheduler,
			std::move(uploaded_texture),
			test_case.region,
			layout
		);
		auto readback_operation = std::move(readback_sender).connect(
			ReadbackReceiver<Resource>{ readback_state }
		);
		readback_operation.start();
		Wait(*readback_state, std::string(test_case.name) + " texture readback");

		if (readback_state->data.size() != expected.size()) {
			throw std::runtime_error("Texture readback returned an unexpected byte count");
		}
		auto images = static_cast<std::size_t>(test_case.region.depth) *
			test_case.region.array_layer_count;
		auto pixel_row_size = static_cast<std::size_t>(test_case.region.width) * 4u;
		for (std::size_t image = 0u; image < images; ++image) {
			for (std::uint32_t row = 0u; row < test_case.region.height; ++row) {
				auto offset = (image * test_case.rows_per_image + row) * RowPitch;
				if (!std::equal(
					expected.begin() + offset,
					expected.begin() + offset + pixel_row_size,
					readback_state->data.begin() + offset
				)) {
					throw std::runtime_error(
						std::string(test_case.name) + " texture readback pixels differ"
					);
				}
			}
		}
	}

	template <class Instance>
	void RunInstanceBackend() {
		auto version = fyuu_rhi::Version{ 0u, 1u, 0u, 0u };
		Instance::Initialize("FyuuRHITextureTransferTests", version, "FyuuEngine", version);
		auto* instance = Instance::Get();
		if (!instance) {
			throw std::runtime_error("RHI instance initialization failed");
		}
		auto physical_devices = instance->EnumeratePhysicalDevices();
		auto physical_device = fyuu_rhi::BestPerformance(physical_devices);
		auto logical_device = physical_device.CreateLogicalDevice();
		auto scheduler = logical_device.CreateScheduler(CopySchedulerDescriptor());
		for (auto const& test_case : TextureTransferCases) {
			RunTextureTransfer(logical_device, scheduler, test_case);
		}
	}

#if defined(_WIN32)
	constexpr wchar_t HiddenWindowClassName[] = L"FyuuRHITextureTransferWindow";

	void RegisterHiddenWindowClass() {
		WNDCLASSW window_class{
			.lpfnWndProc = DefWindowProcW,
			.hInstance = GetModuleHandleW(nullptr),
			.lpszClassName = HiddenWindowClassName
		};
		if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
			throw std::runtime_error("Could not register the OpenGL test window class");
		}
	}

	class HiddenWindow {
		HWND m_impl = nullptr;

	public:
		HiddenWindow() {
			static std::once_flag class_flag;
			std::call_once(class_flag, RegisterHiddenWindowClass);
			m_impl = CreateWindowExW(
				0u, HiddenWindowClassName, L"FyuuRHI texture transfer", WS_OVERLAPPEDWINDOW,
				CW_USEDEFAULT, CW_USEDEFAULT, 64, 64, nullptr, nullptr,
				GetModuleHandleW(nullptr), nullptr
			);
			if (!m_impl) {
				throw std::runtime_error("Could not create the OpenGL test window");
			}
		}

		HiddenWindow(HiddenWindow const&) = delete;
		HiddenWindow& operator=(HiddenWindow const&) = delete;
		~HiddenWindow() noexcept {
			if (m_impl) {
				DestroyWindow(m_impl);
			}
		}

		operator HWND() const noexcept { return m_impl; }
	};

	void RunOpenGL() {
		HiddenWindow window;
		auto version = fyuu_rhi::Version{ 0u, 1u, 0u, 0u };
		fyuu_rhi::OpenGLInstance::Initialize(
			"FyuuRHITextureTransferTests", version, "FyuuEngine", version,
			static_cast<HWND>(window)
		);
		auto* instance = fyuu_rhi::OpenGLInstance::Get();
		auto physical_device = instance->EnumeratePhysicalDevices();
		auto logical_device = physical_device.CreateLogicalDevice();
		auto scheduler = logical_device.CreateScheduler(CopySchedulerDescriptor());
		for (auto const& test_case : TextureTransferCases) {
			RunTextureTransfer(logical_device, scheduler, test_case);
		}
	}
#endif
}

int main(int argc, char** argv) {
	if (argc != 2) {
		std::cerr << "Expected one backend name\n";
		return 2;
	}
	std::string_view backend = argv[1];
	try {
#if defined(_WIN32)
		if (backend == "d3d12") {
			RunInstanceBackend<fyuu_rhi::D3D12Instance>();
		}
		else
#endif
#if !defined(__APPLE__)
		if (backend == "vulkan") {
			RunInstanceBackend<fyuu_rhi::VulkanInstance>();
		}
		else if (backend == "opengl") {
#if defined(_WIN32)
			RunOpenGL();
#else
			throw std::runtime_error("OpenGL integration test requires a native test surface");
#endif
		}
		else
#endif
		if (backend == "webgpu") {
			RunInstanceBackend<fyuu_rhi::WebGPUInstance>();
		}
		else {
			std::cerr << "Unknown backend: " << backend << '\n';
			return 2;
		}
	}
	catch (std::exception const& error) {
		std::cerr << backend << ": " << error.what() << '\n';
		return 1;
	}
	return 0;
}
