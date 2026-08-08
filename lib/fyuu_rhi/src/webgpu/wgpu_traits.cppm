/* the webgpu pattern
module;
#include <version>
#if !defined(__cpp_lib_modules)

#endif // !defined(__cpp_lib_modules)
#include <webgpu/webgpu_cpp.h>
export module fyuu_rhi:;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
namespace fyuu_rhi::webgpu {

}

*/

module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>

#include <optional>
#include <string_view>
#include <variant>

#include <compare>
#include <format>
#include <span>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <wayland-client-core.h>
#include <wayland-util.h>
#elif defined(__ANDROID__)
#include <android/native_window.h>
#include <android/android_native_app_glue.h>
#endif // defined(_WIN32)
#include <webgpu/webgpu_cpp.h>
export module fyuu_rhi:webgpu_traits;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core_types;
import :resource_types;
import :sampler_types;
import :pipeline_types;
import :native_pipeline_binding;
import :execution_types;

namespace fyuu_rhi::webgpu {
	using namespace fyuu_rhi::pipeline;
	struct Backend {
#if defined(_WIN32)
		using PlatformHandle = HWND;
#elif defined(__linux__)
		struct X11PlatformHandle {
			Display* display;
			Window window;

			std::strong_ordering operator<=>(X11PlatformHandle const&) const noexcept = default;
		};

		struct WaylandPlatformHandle {
			wl_display* display;
			wl_surface* surface;

			std::strong_ordering operator<=>(WaylandPlatformHandle const&) const noexcept = default;
		};

		using PlatformHandle = std::variant<X11PlatformHandle, WaylandPlatformHandle>;
#elif defined(__ANDROID__)
		using PlatformHandle = ANativeWindow*;
#endif // defined(_WIN32)

		struct SchedulerContext {
			/// Owns the device, queue and the dedicated event pump. Defined in the
			/// exported traits so factories can construct it.
			struct Implementation {
				wgpu::Instance instance;
				wgpu::Device device;

				/// Per-window presentation state; the current texture is acquired
				/// in phase 1 and must be presented before the next acquisition.
				struct SurfaceState {
					PlatformHandle handle{};
					wgpu::Surface surface;
					std::uint32_t width = 0u;
					std::uint32_t height = 0u;
					wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
				};
				std::vector<SurfaceState> surfaces;
				std::mutex surface_mutex;
			};

			std::shared_ptr<Implementation> impl;

			std::strong_ordering operator<=>(SchedulerContext const&) const noexcept = default;
		};

		class CompletionToken {
			/// Defined in the execution partition. Exclusively owned by the token;
			/// the completion callback holds a raw pointer and fires before the
			/// token is destroyed (the front end polls to completion first).
			struct Implementation;
			struct Deleter {
				void operator()(Implementation* impl) const noexcept;
			};
			std::unique_ptr<Implementation, Deleter> impl;

			explicit CompletionToken(std::unique_ptr<Implementation, Deleter> impl_) noexcept
				: impl(std::move(impl_)) {}
			friend struct Backend;

		public:
			CompletionToken() noexcept = default;
			CompletionToken(CompletionToken const&) = delete;
			CompletionToken& operator=(CompletionToken const&) = delete;
			CompletionToken(CompletionToken&&) noexcept = default;
			CompletionToken& operator=(CompletionToken&&) noexcept = default;
			~CompletionToken() noexcept;

			[[nodiscard]] bool Poll() noexcept;
			[[nodiscard]] std::exception_ptr Error() const noexcept;
			[[nodiscard]] bool IsStopped() const noexcept;
		};

		using Instance = wgpu::Instance;
		using PhysicalDevice = wgpu::Adapter;
		using Surface = wgpu::Surface;
		struct LogicalDevice {
			wgpu::Device impl;
			wgpu::Adapter adapter;
		};

		using Resource = std::variant<std::monostate, wgpu::Buffer, wgpu::Texture>;

		struct BufferView {
			wgpu::Buffer buf;
			std::size_t offset;
			std::size_t range;
		};
		using View = std::variant<std::monostate, wgpu::TextureView, BufferView>;

		using Sampler = wgpu::Sampler;

		struct Pipeline {
			std::vector<wgpu::BindGroupLayout> bind_group_layouts;
			std::vector<PipelineBindingMetadata> bindings;
			std::variant<wgpu::RenderPipeline, wgpu::ComputePipeline> impl;
			bool compute = false;
		};

		struct PipelineResourceGroup {
			NativePipelineResourceGroup<Backend> native;
			wgpu::BindGroup impl;
			std::uint32_t space = 0u;
		};

		static CompletionToken ExecuteCommands(
			SchedulerContext const& scheduler,
			execution::ExecutionPlan const& plan,
			std::span<PlatformHandle const> presentation_targets,
			std::span<std::reference_wrapper<Resource> const> resources,
			std::span<std::reference_wrapper<View> const> views,
			std::span<std::reference_wrapper<Sampler> const> samplers,
			std::span<std::reference_wrapper<Pipeline> const> pipelines,
			std::span<std::reference_wrapper<PipelineResourceGroup> const> resource_groups,
			execution::StopTokenView stop_token
		);

		static SchedulerContext CreateScheduler(LogicalDevice const& ld);

		static wgpu::Instance CreateInstance(
			std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver
#if defined(__ANDROID__)
			, android_app* android_app
#endif // defined(__ANDROID__)
		);

		static wgpu::Adapter EnumeratePhysicalDevices(wgpu::Instance const& instance);

		static PhysicalDeviceInfo GetPhysicalDeviceInfo(wgpu::Adapter const& phys_dev);

		static LogicalDevice CreateLogicalDevice(wgpu::Adapter const& adapter);

		static Resource CreateBuffer(LogicalDevice const& ld, std::size_t size_in_bytes, ResourceFlags const& flags);

		static Resource CreateTexture(LogicalDevice const& ld, std::size_t width, std::size_t height, std::size_t depth_arr_layers, std::size_t mip_lvl_cnt, ResourceFlags const& flags);

		static View CreateTextureView(LogicalDevice const& ld, Resource const& res, std::size_t base_mip_lvl, std::size_t mip_lvl_cnt, std::size_t base_arr_layer, std::size_t arr_layer_cnt, ResourceFlags const& flags);

		static View CreateBufferView(LogicalDevice const& ld, Resource const& buf, std::size_t offset, std::size_t range, ResourceFlags const& flags);

		static wgpu::Sampler CreateSampler(LogicalDevice const& ld, SamplerDescriptor const& desc);

		static Pipeline CreateGraphicsPipeline(LogicalDevice const& ld, GraphicsPipelineDescriptor const& descriptor);
		static Pipeline CreateComputePipeline(LogicalDevice const& ld, ComputePipelineDescriptor const& descriptor);

		static PipelineResourceGroup CreatePipelineResourceGroup(
			LogicalDevice const& ld,
			Pipeline const& pipeline,
			std::uint32_t space,
			std::span<NativePipelineResourceBinding<Backend> const> bindings
		);

	};

}
