module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <atomic>
#include <cstddef>
#include <exception>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <cstdint>

#include <variant>
#endif // !defined(__cpp_lib_modules)
#include <dawn/webgpu_cpp.h>

module fyuu_rhi:webgpu_data;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :execution;
import :pipeline;

namespace fyuu_rhi::webgpu {

	struct Instance {
		wgpu::Instance impl;
	};

	struct PhysicalDevice {
		wgpu::Instance instance;
		wgpu::Adapter adapter;
	};

	struct LogicalDevice {
		wgpu::Instance instance;
		wgpu::Adapter adapter;
		wgpu::Device impl;
	};

	struct CompletionState {
		std::atomic_bool complete = false;
		std::atomic_bool stopped = false;
		std::mutex mutex;
		std::exception_ptr error;
	};

	struct CompletionToken {
		wgpu::Instance instance;
		std::shared_ptr<CompletionState> state;
	};

	struct CommandSchedulerContext {
		struct SurfaceState {
			execution::PlatformHandle handle;
			wgpu::Surface surface;
			std::uint32_t width = 0u;
			std::uint32_t height = 0u;
			wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
		};

		wgpu::Instance instance;
		wgpu::Device device;
		std::vector<SurfaceState> surfaces;
		std::mutex surfaces_mutex;

		CommandSchedulerContext(
			wgpu::Instance const& instance,
			wgpu::Device const& device
		) noexcept
			: instance(instance),
			device(device) {
		}

		CommandSchedulerContext(CommandSchedulerContext const&) = delete;
		CommandSchedulerContext& operator=(CommandSchedulerContext const&) = delete;

		CommandSchedulerContext(CommandSchedulerContext&& other) noexcept
			: instance(std::move(other.instance)),
			device(std::move(other.device)),
			surfaces(std::move(other.surfaces)) {
		}
	};

	struct Resource {
		std::variant<wgpu::Buffer, wgpu::Texture> impl;
	};

	struct View {
		struct Buffer {
			wgpu::Buffer impl;
			std::size_t offset;
			std::size_t size;
		};

		std::variant<Buffer, wgpu::TextureView> impl;
	};

	struct Sampler {
		wgpu::Sampler impl;
	};

	struct Pipeline {
		wgpu::Device device;
		std::vector<wgpu::BindGroupLayout> bind_group_layouts;
		std::vector<pipeline::BindingMetadata> bindings;
		std::variant<
			std::monostate,
			wgpu::RenderPipeline,
			wgpu::ComputePipeline
		> impl;
	};

	struct PipelineResourceGroup {
		std::uint32_t space;
		wgpu::BindGroup impl;
	};

} // namespace fyuu_rhi::webgpu::data
