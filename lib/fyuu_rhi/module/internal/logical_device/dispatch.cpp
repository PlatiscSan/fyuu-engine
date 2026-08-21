module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:logical_device_dispatch;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :logical_device;

namespace fyuu_rhi {

	template <class NativeLogicalDevice>
	struct CreateBuffer {
		NativeLogicalDevice* logical_device;

		Resource operator()(std::size_t, ResourceFlags const&) const {
			throw std::runtime_error("Buffer creation is not implemented for this backend");
		}
	};

	template <class NativeLogicalDevice>
	struct CreateTexture {
		NativeLogicalDevice* logical_device;

		Resource operator()(
			std::size_t,
			std::size_t,
			std::size_t,
			std::size_t,
			ResourceFlags const&
		) const {
			throw std::runtime_error("Texture creation is not implemented for this backend");
		}
	};

	template <class NativeLogicalDevice>
	struct CreateSampler {
		NativeLogicalDevice* logical_device;

		Sampler operator()(SamplerDescriptor const&) const {
			throw std::runtime_error("Sampler creation is not implemented for this backend");
		}
	};

	template <class NativeLogicalDevice>
	struct CreateGraphicsPipeline {
		NativeLogicalDevice* logical_device;

		Pipeline operator()(pipeline::GraphicsPipelineDescriptor const&) const {
			throw std::runtime_error("Graphics pipeline creation is not implemented for this backend");
		}
	};

	template <class NativeLogicalDevice>
	struct CreateComputePipeline {
		NativeLogicalDevice* logical_device;

		Pipeline operator()(pipeline::ComputePipelineDescriptor const&) const {
			throw std::runtime_error("Compute pipeline creation is not implemented for this backend");
		}
	};

	template <class NativeLogicalDevice>
	struct CreateScheduler {
		NativeLogicalDevice* logical_device;

		execution::CommandScheduler operator()() const {
			throw std::runtime_error("Scheduler creation is not implemented for this backend");
		}
	};

} // namespace fyuu_rhi
