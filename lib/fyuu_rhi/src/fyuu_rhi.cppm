module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <vector>
#include <span>
#include <concepts>
#endif // !defined(__cpp_lib_modules)
export module fyuu_rhi;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
export import :log;
export import :core_types;
export import :execution;
#if defined(_WIN32)
import :d3d12_traits;
#endif // defined(_WIN32)
#if defined(__APPLE__)
import :metal_traits;
#else
import :vulkan_traits;
import :opengl_traits;
#endif // defined(__APPLE__)
import :webgpu_traits;

export import :resource_types;
export import :resource;
export import :view;
export import :sampler_types;
export import :sampler;
export import :pipeline_types;
export import :pipeline;
export import :logical_device;
import :physical_device;
import :instance;

namespace fyuu_rhi {

	std::size_t PhysicalDeviceTypeScore(PhysicalDeviceInfo::Type type) {
		using Type = PhysicalDeviceInfo::Type;
		switch (type) {
		case Type::DiscreteGPU: return 4u;
		case Type::IntegratedGPU: return 3u;
		case Type::CPU: return 2u;
		case Type::Virtual: return 1u;
		default: return 0u;
		}
	}
	
#if defined(_WIN32)
	export using D3D12Instance = Instance<d3d12::Backend>;
	export using D3D12PhysicalDevice = PhysicalDevice<d3d12::Backend>;
	export using D3D12LogicalDevice = LogicalDevice<d3d12::Backend>;
	export using D3D12Resource = Resource<d3d12::Backend>;
	export using D3D12View = View<d3d12::Backend>;
	export using D3D12Sampler = Sampler<d3d12::Backend>;
#endif // defined(_WIN32)
#if defined(__APPLE__)
	export using MetalInstance = Instance<metal::Backend>;
	export using MetalPhysicalDevice = PhysicalDevice<metal::Backend>;
	export using MetalLogicalDevice = LogicalDevice<metal::Backend>;
	export using MetalResource = Resource<metal::Backend>;
	export using MetalSampler = Sampler<metal::Backend>;
#else
	export using VulkanInstance = Instance<vulkan::Backend>;
	export using VulkanPhysicalDevice = PhysicalDevice<vulkan::Backend>;
	export using VulkanLogicalDevice = LogicalDevice<vulkan::Backend>;
	export using VulkanResource = Resource<vulkan::Backend>;
	export using VulkanView = View<vulkan::Backend>;
	export using VulkanSampler = Sampler<vulkan::Backend>;

	export using OpenGLInstance = Instance<opengl::Backend>;
	export using OpenGLPhysicalDevice = PhysicalDevice<opengl::Backend>;
	export using OpenGLLogicalDevice = LogicalDevice<opengl::Backend>;
	export using OpenGLResource = Resource<opengl::Backend>;
	export using OpenGLView = View<opengl::Backend>;
	export using OpenGLSampler = Sampler<opengl::Backend>;
#endif // defined(__APPLE__)
	export using WebGPUInstance = Instance<webgpu::Backend>;
	export using WebGPUPhysicalDevice = PhysicalDevice<webgpu::Backend>;
	export using WebGPULogicalDevice = LogicalDevice<webgpu::Backend>;
	export using WebGPUResource = Resource<webgpu::Backend>;
	export using WebGPUView = View<webgpu::Backend>;
	export using WebGPUSampler = Sampler<webgpu::Backend>;

	export template <class T> T BestPerformance(std::span<T const> phys_devs) {

		if (phys_devs.empty()) {
			throw std::invalid_argument("BestPerformace(): no available physical devices");
		}

		std::size_t best_idx = 0;
		static_assert(std::same_as<decltype(phys_devs[0].GetInfo()), PhysicalDeviceInfo>,
			"GetInfo() does not return PhysicalDeviceInfo");
			
		auto best_info = phys_devs[0].GetInfo();
		std::size_t best_score = PhysicalDeviceTypeScore(best_info.type);
		std::size_t best_mem = best_info.dedicated_memory.value_or(0);

		for (std::size_t i = 1; i < phys_devs.size(); ++i) {
			auto info = phys_devs[i].GetInfo();
			std::size_t score = PhysicalDeviceTypeScore(info.type);
			std::size_t mem = info.dedicated_memory.value_or(0);

			if (score > best_score || (score == best_score && mem > best_mem)) {
				best_idx = i;
				best_score = score;
				best_mem = mem;
			}
		}

		return phys_devs[best_idx];
	}

	export template <class T> T BestPerformance(std::vector<T> const& phys_devs) {
		return BestPerformance(std::span<T const>(phys_devs));
	}

	export template <class T> T BestPerformance(std::vector<T>& phys_devs) {
		return BestPerformance(const_cast<std::vector<T> const&>(phys_devs));
	}

	export template <class T> T BestPerformance(T& phys_devs) {
		return phys_devs;
	}

} // namespace fyuu_rhi

namespace fyuu_rhi::execution {

#if defined(_WIN32)
	export using D3D12CommandScheduler = CommandScheduler<d3d12::Backend>;
	export using D3D12CommandGraphBuilder = CommandGraphBuilder<d3d12::Backend>;
	export using D3D12CommandGraphResources = CommandGraphResources<d3d12::Backend>;
	export template <class Receiver>
	using D3D12CommandGraphBindings = CommandGraphBindings<d3d12::Backend, Receiver>;
#endif // defined(_WIN32)
#if defined(__APPLE__)
	export using MetalCommandScheduler = CommandScheduler<metal::Backend>;
	export using MetalCommandGraphBuilder = CommandGraphBuilder<metal::Backend>;
	export using MetalCommandGraphResources = CommandGraphResources<metal::Backend>;
	export template <class Receiver>
	using MetalCommandGraphBindings = CommandGraphBindings<metal::Backend, Receiver>;
#else
	export using VulkanCommandScheduler = CommandScheduler<vulkan::Backend>;
	export using VulkanCommandGraphBuilder = CommandGraphBuilder<vulkan::Backend>;
	export using VulkanCommandGraphResources = CommandGraphResources<vulkan::Backend>;
	export template <class Receiver>
	using VulkanCommandGraphBindings = CommandGraphBindings<vulkan::Backend, Receiver>;
	export using OpenGLCommandScheduler = CommandScheduler<opengl::Backend>;
	export using OpenGLCommandGraphBuilder = CommandGraphBuilder<opengl::Backend>;
	export using OpenGLCommandGraphResources = CommandGraphResources<opengl::Backend>;
	export template <class Receiver>
	using OpenGLCommandGraphBindings = CommandGraphBindings<opengl::Backend, Receiver>;
#endif // defined(__APPLE__)
	export using WebGPUCommandScheduler = CommandScheduler<webgpu::Backend>;
	export using WebGPUCommandGraphBuilder = CommandGraphBuilder<webgpu::Backend>;
	export using WebGPUCommandGraphResources = CommandGraphResources<webgpu::Backend>;
	export template <class Receiver>
	using WebGPUCommandGraphBindings = CommandGraphBindings<webgpu::Backend, Receiver>;

}

namespace fyuu_rhi::pipeline {

#if defined(_WIN32)
	export using D3D12Pipeline = Pipeline<d3d12::Backend>;
	export using D3D12PipelineResourceGroup = PipelineResourceGroup<d3d12::Backend>;
#endif // defined(_WIN32)
#if !defined(__APPLE__)
	export using VulkanPipeline = Pipeline<vulkan::Backend>;
	export using VulkanPipelineResourceGroup = PipelineResourceGroup<vulkan::Backend>;

	export using OpenGLPipeline = Pipeline<opengl::Backend>;
	export using OpenGLPipelineResourceGroup = PipelineResourceGroup<opengl::Backend>;
#endif // defined(__APPLE__)
	export using WebGPUPipeline = Pipeline<webgpu::Backend>;
	export using WebGPUPipelineResourceGroup = PipelineResourceGroup<webgpu::Backend>;

}
