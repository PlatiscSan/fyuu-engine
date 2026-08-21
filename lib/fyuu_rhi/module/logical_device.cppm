module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <memory>
#include <utility>

#include <cstdint>

#include <span>
#endif // !defined(__cpp_lib_modules)
export module fyuu_rhi:logical_device;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource;
import :sampler;
import :pipeline;
import :execution;

export namespace fyuu_rhi {

	class LogicalDevice {
	public:
		using UniqueHandle = std::unique_ptr<
			struct LogicalDeviceImplementation,
			void(*)(struct LogicalDeviceImplementation*)
		>;

	private:
		UniqueHandle m_impl;

	public:
		LogicalDevice() noexcept = default;

		LogicalDevice(UniqueHandle&& impl) noexcept
			: m_impl(std::move(impl)) {
		}

		explicit operator bool() const noexcept {
			return static_cast<bool>(m_impl);
		}

		Resource CreateBuffer(std::size_t size_in_bytes, ResourceFlags const& flags);

		Resource CreateTexture(
			std::size_t width,
			std::size_t height,
			std::size_t depth_arr_layers,
			std::size_t mip_lvl_cnt,
			ResourceFlags const& flags
		);

		Sampler CreateSampler(SamplerDescriptor const& descriptor);

		Pipeline CreateGraphicsPipeline(pipeline::GraphicsPipelineDescriptor const& descriptor);

		Pipeline CreateComputePipeline(pipeline::ComputePipelineDescriptor const& descriptor);

		execution::CommandScheduler CreateScheduler();

	};

} // namespace fyuu_rhi
