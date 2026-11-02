module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>

#include <span>
#endif // !defined(__cpp_lib_modules)
export module fyuu_rhi:logical_device;
import :fast_pimpl;
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
		using ImplementationStorage = details::FastPimpl<struct LogicalDeviceImplementation, 384u>;

	private:
		ImplementationStorage m_impl;

	public:
		LogicalDevice() noexcept;

		LogicalDevice(ImplementationStorage&& impl) noexcept;

		LogicalDevice(LogicalDevice const&) = delete;
		LogicalDevice& operator=(LogicalDevice const&) = delete;
		LogicalDevice(LogicalDevice&& other) noexcept;
		LogicalDevice& operator=(LogicalDevice&& other) noexcept;
		~LogicalDevice() noexcept;

		explicit operator bool() const noexcept {
			return static_cast<bool>(m_impl);
		}

		/// Creates a buffer of exactly @p size_in_bytes with the declared usage flags.
		Resource CreateBuffer(std::size_t size_in_bytes, ResourceFlags const& flags);

		/**
		 * @brief Creates a texture; dimension, format, samples, memory, and usage come from flags.
		 * @param clear_value Default attachment clear; actual initialization is deferred to a ClearDefault command.
		 * @note Explicit BeginRendering clears may override this default, possibly losing native fast-clear optimization.
		 */
		Resource CreateTexture(
			std::size_t width,
			std::size_t height,
			std::size_t depth_arr_layers,
			std::size_t mip_lvl_cnt,
			ResourceFlags const& flags,
			TextureClearValue const& clear_value = {}
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
