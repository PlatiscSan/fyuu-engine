module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :logical_device;
import :logical_device_dispatch;
import :logical_device_factory;
#if defined(_WIN32)
import :d3d12_logical_device;
#endif // defined(_WIN32)
#if defined(__APPLE__)
import :metal_command_scheduler;
import :metal_logical_device;
#else
import :opengl_command_scheduler;
import :opengl_logical_device;
import :vulkan_logical_device;
import :vulkan_command_scheduler;
#endif // defined(__APPLE__)
import :webgpu_logical_device;
import :webgpu_command_scheduler;

namespace fyuu_rhi {

	Resource LogicalDevice::CreateBuffer(std::size_t size_in_bytes, ResourceFlags const& flags) {
		if (!m_impl) {
			throw std::runtime_error("Cannot create a buffer from an empty logical device");
		}
		return std::visit(
			[size_in_bytes, &flags]<class NativeLogicalDevice>(NativeLogicalDevice& native) {
				return fyuu_rhi::CreateBuffer<NativeLogicalDevice>{ &native }(size_in_bytes, flags);
			},
			m_impl->native
		);
	}

	Resource LogicalDevice::CreateTexture(
		std::size_t width,
		std::size_t height,
		std::size_t depth_arr_layers,
		std::size_t mip_lvl_cnt,
		ResourceFlags const& flags
	) {
		if (!m_impl) {
			throw std::runtime_error("Cannot create a texture from an empty logical device");
		}
		return std::visit(
			[&]<class NativeLogicalDevice>(NativeLogicalDevice& native) {
				return fyuu_rhi::CreateTexture<NativeLogicalDevice>{ &native }(
					width,
					height,
					depth_arr_layers,
					mip_lvl_cnt,
					flags
				);
			},
			m_impl->native
		);
	}

	Sampler LogicalDevice::CreateSampler(SamplerDescriptor const& descriptor) {
		if (!m_impl) {
			throw std::runtime_error("Cannot create a sampler from an empty logical device");
		}
		return std::visit(
			[&descriptor]<class NativeLogicalDevice>(NativeLogicalDevice& native)
			{
				return fyuu_rhi::CreateSampler<NativeLogicalDevice>{ &native }(descriptor);
			},
			m_impl->native
		);
	}

	Pipeline LogicalDevice::CreateGraphicsPipeline(
		pipeline::GraphicsPipelineDescriptor const& descriptor
	) {
		if (!m_impl) {
			throw std::runtime_error(
				"Cannot create a graphics pipeline from an empty logical device"
			);
		}
		return std::visit(
			[&descriptor]<class NativeLogicalDevice>(NativeLogicalDevice& native)
			{
				return fyuu_rhi::CreateGraphicsPipeline<NativeLogicalDevice>{ &native }(
					descriptor
				);
			},
			m_impl->native
		);
	}

	Pipeline LogicalDevice::CreateComputePipeline(
		pipeline::ComputePipelineDescriptor const& descriptor
	) {
		if (!m_impl) {
			throw std::runtime_error(
				"Cannot create a compute pipeline from an empty logical device"
			);
		}
		return std::visit(
			[&descriptor]<class NativeLogicalDevice>(NativeLogicalDevice& native)
			{
				return fyuu_rhi::CreateComputePipeline<NativeLogicalDevice>{ &native }(
					descriptor
				);
			},
			m_impl->native
		);
	}

	execution::CommandScheduler LogicalDevice::CreateScheduler() {
		if (!m_impl) {
			throw std::runtime_error("Cannot create a scheduler from an empty logical device");
		}
		return std::visit(
			[]<class NativeLogicalDevice>(NativeLogicalDevice& native)
			{
				return fyuu_rhi::CreateScheduler<NativeLogicalDevice>{ &native }();
			},
			m_impl->native
		);
	}

} // namespace fyuu_rhi
