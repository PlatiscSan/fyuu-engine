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

	/**
	 * @brief Move-only factory and ownership boundary for GPU objects.
	 *
	 * Resources, pipelines, samplers, resource groups, and scheduler work submitted
	 * together must originate from the same LogicalDevice.
	 */
	class LogicalDevice {
	public:
		using UniqueHandle = std::unique_ptr<
			struct LogicalDeviceImplementation,
			void(*)(struct LogicalDeviceImplementation*)
		>;

	private:
		UniqueHandle m_impl;

	public:
		LogicalDevice() noexcept
			: m_impl(nullptr, [](LogicalDeviceImplementation*) {}) {

		}

		LogicalDevice(UniqueHandle&& impl) noexcept
			: m_impl(std::move(impl)) {
		}

		explicit operator bool() const noexcept {
			return static_cast<bool>(m_impl);
		}

		/// Creates a buffer of exactly @p size_in_bytes with the declared usage flags.
		Resource CreateBuffer(std::size_t size_in_bytes, ResourceFlags const& flags);

		/// Creates a texture; dimension, format, samples, memory, and usage come from flags.
		Resource CreateTexture(
			std::size_t width,
			std::size_t height,
			std::size_t depth_arr_layers,
			std::size_t mip_lvl_cnt,
			ResourceFlags const& flags
		);

		/// Creates immutable sampling state.
		Sampler CreateSampler(SamplerDescriptor const& descriptor);

		/// Compiles and creates a graphics pipeline from a Slang program and fixed state.
		Pipeline CreateGraphicsPipeline(pipeline::GraphicsPipelineDescriptor const& descriptor);

		/// Compiles and creates a compute pipeline from a Slang program.
		Pipeline CreateComputePipeline(pipeline::ComputePipelineDescriptor const& descriptor);

		/// Creates a copyable command scheduler sharing this device's native queues.
		execution::CommandScheduler CreateScheduler();

	};

} // namespace fyuu_rhi
