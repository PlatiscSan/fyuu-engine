module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>

#include <span>
#include <variant>
#endif // !defined(__cpp_lib_modules)

#include <frozen/unordered_map.h>

module fyuu_rhi;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core;
import :instance;
import :instance_dispatch;
import :instance_factory;
#if defined(__APPLE__)
import :metal_data;
import :metal_instance;
#endif // defined(__APPLE__)
#if defined(_WIN32)
import :d3d12_data;
import :d3d12_instance;
import :opengl_instance_wgl;
#endif // defined(_WIN32)
#if !defined(__APPLE__)
import :opengl_data;
import :vulkan_data;
import :vulkan_instance;
#endif // !defined(__APPLE__)
#if defined(__linux__) && !defined(__ANDROID__)
import :opengl_instance_egl;
import :opengl_instance_glx;
#endif // defined(__linux__) && !defined(__ANDROID__)
#if defined(__ANDROID__)
import :opengl_instance_egl;
#endif // defined(__ANDROID__)
import :webgpu_data;
import :webgpu_instance;

namespace fyuu_rhi {
	bool IsInitialized() noexcept;
}

namespace {
	using NativeInstance = std::variant<
	    std::monostate,
#if defined(_WIN32)
	    fyuu_rhi::d3d12::Instance,
	    fyuu_rhi::opengl::Instance,
#elif defined(__APPLE__)
	    fyuu_rhi::metal::Instance,
#elif defined(__linux__) && !defined(__ANDROID__)
	    fyuu_rhi::opengl::EGLInstance,
	    fyuu_rhi::opengl::GLXInstance,
#elif defined(__ANDROID__)
	    fyuu_rhi::opengl::Instance,
#endif
#if !defined(__APPLE__)
	    fyuu_rhi::vulkan::Instance,
#endif // !defined(__APPLE__)
	    fyuu_rhi::webgpu::Instance>;
} // namespace

namespace fyuu_rhi {
	struct InstanceImplementation {
		NativeInstance native;
	};
} // namespace fyuu_rhi

namespace {
	union InstanceStorage {
		fyuu_rhi::InstanceImplementation implementation;

		InstanceStorage()
			: implementation{} {
		}

		// FyuuRHI is linked into a DLL. Releasing a process-level graphics instance
		// from DLL_PROCESS_DETACH would enter DXGI, Vulkan, Dawn, or an OpenGL loader
		// while the Windows loader lock is held. Keep the native root alive until the
		// process reclaims it instead; the active union member remains valid for the
		// entire static-storage lifetime.
		~InstanceStorage() noexcept {
		}
	};

#if defined(_WIN32)
	InstanceStorage d3d12_storage;
	fyuu_rhi::Instance d3d12_instance{&d3d12_storage.implementation};
#endif // defined(_WIN32)

#if !defined(__APPLE__)
	InstanceStorage vulkan_storage;
	fyuu_rhi::Instance vulkan_instance{&vulkan_storage.implementation};

	InstanceStorage opengl_storage;
	fyuu_rhi::Instance opengl_instance{&opengl_storage.implementation};
#else
	InstanceStorage metal_storage;
	fyuu_rhi::Instance metal_instance{&metal_storage.implementation};
#endif // !defined(__APPLE__)

	InstanceStorage webgpu_storage;
	fyuu_rhi::Instance webgpu_instance{&webgpu_storage.implementation};

#if defined(_WIN32)
	fyuu_rhi::Instance& RequestD3D12() {
		auto& implementation = d3d12_storage.implementation;
		if (std::holds_alternative<std::monostate>(implementation.native)) {
			implementation.native.emplace<fyuu_rhi::d3d12::Instance>(
				fyuu_rhi::CreateInstance<fyuu_rhi::d3d12::Instance>{}()
			);
		}
		return d3d12_instance;
	}
#endif // defined(_WIN32)

#if !defined(__APPLE__)
	fyuu_rhi::Instance& RequestVulkan() {
		auto& implementation = vulkan_storage.implementation;
		if (std::holds_alternative<std::monostate>(implementation.native)) {
			implementation.native.emplace<fyuu_rhi::vulkan::Instance>(
				fyuu_rhi::CreateInstance<fyuu_rhi::vulkan::Instance>{}()
			);
		}
		return vulkan_instance;
	}
#else
	fyuu_rhi::Instance& RequestMetal() {
		auto& implementation = metal_storage.implementation;
		if (std::holds_alternative<std::monostate>(implementation.native)) {
			implementation.native.emplace<fyuu_rhi::metal::Instance>(
				fyuu_rhi::CreateInstance<fyuu_rhi::metal::Instance>{}()
			);
		}
		return metal_instance;
	}
#endif // !defined(__APPLE__)

	fyuu_rhi::Instance& RequestWebGPU() {
#if defined(_WIN32) && !defined(NDEBUG)
		// Dawn uses D3D12 on Windows. Configure the process-wide D3D12 debug
		// layer before Dawn creates its first device; enabling it afterwards
		// invalidates existing devices and prevents a later native backend switch.
		(void)RequestD3D12();
#endif // defined(_WIN32) && !defined(NDEBUG)
		auto& implementation = webgpu_storage.implementation;
		if (std::holds_alternative<std::monostate>(implementation.native)) {
			implementation.native.emplace<fyuu_rhi::webgpu::Instance>(
				fyuu_rhi::CreateInstance<fyuu_rhi::webgpu::Instance>{}()
			);
		}
		return webgpu_instance;
	}

#if defined(__linux__) && !defined(__ANDROID__)
	fyuu_rhi::Instance& RequestOpenGL() {
		auto& implementation = opengl_storage.implementation;
		if (fyuu_rhi::IsWayland()) {
			if (std::holds_alternative<std::monostate>(implementation.native)) {
				implementation.native.emplace<fyuu_rhi::opengl::EGLInstance>(
					fyuu_rhi::CreateInstance<fyuu_rhi::opengl::EGLInstance>{}()
				);
			}
			return opengl_instance;
		}
		if (std::holds_alternative<std::monostate>(implementation.native)) {
			implementation.native.emplace<fyuu_rhi::opengl::GLXInstance>(
				fyuu_rhi::CreateInstance<fyuu_rhi::opengl::GLXInstance>{}()
			);
		}
		return opengl_instance;
	}
#elif !defined(__APPLE__)
	fyuu_rhi::Instance& RequestOpenGL() {
		auto& implementation = opengl_storage.implementation;
		if (std::holds_alternative<std::monostate>(implementation.native)) {
			implementation.native.emplace<fyuu_rhi::opengl::Instance>(
				fyuu_rhi::CreateInstance<fyuu_rhi::opengl::Instance>{}()
			);
		}
		return opengl_instance;
	}
#endif // defined(__linux__) && !defined(__ANDROID__)

} // namespace

namespace fyuu_rhi {
	std::vector<PhysicalDevice> Instance::EnumeratePhysicalDevices() const {
		return std::visit(
		    []<class Native>(Native const& native) {
			    if constexpr (std::same_as<Native, std::monostate>) {
				    return std::vector<PhysicalDevice>{};
			    } else {
				    return fyuu_rhi::EnumeratePhysicalDevices<Native>{&native}();
			    }
		    },
		    m_impl->native
		);
	}

	void Instance::ShareContextOnThisThread() {
		std::visit(
		    []<class Native>(Native const& native) {
			    if constexpr (!std::same_as<Native, std::monostate>) {
				    ShareContextOnCurrentThread<Native>{&native}();
			    }
		    },
		    m_impl->native
		);
	}

	std::span<Backend const> EnumerateBackends() noexcept {
		static constexpr Backend backends[]{
#if defined(_WIN32)
		    Backend::DirectX12,
#endif // defined(_WIN32)
#if defined(__APPLE__)
		    Backend::Metal,
#else
		    Backend::Vulkan,
		    Backend::OpenGL,
#endif // defined(__APPLE__)
		    Backend::WebGPU
		};
		return backends;
	}

	Instance& RequestInstance(Backend backend) {
		if (!IsInitialized()) {
			throw std::runtime_error("RHI context is not initialized yet");
		}

		using RequestFunction = Instance& (*)();
#if defined(_WIN32)
		static constexpr frozen::unordered_map<Backend, RequestFunction, 4u> requests{
		    {Backend::DirectX12, RequestD3D12},
		    {Backend::Vulkan, RequestVulkan},
		    {Backend::OpenGL, RequestOpenGL},
		    {Backend::WebGPU, RequestWebGPU}
		};
#elif defined(__APPLE__)
		static constexpr frozen::unordered_map<Backend, RequestFunction, 2u> requests{
		    {Backend::Metal, RequestMetal},
		    {Backend::WebGPU, RequestWebGPU}
		};
#elif defined(__linux__) && !defined(__ANDROID__)
		static constexpr frozen::unordered_map<Backend, RequestFunction, 3u> requests{
		    {Backend::Vulkan, RequestVulkan},
		    {Backend::OpenGL, RequestOpenGL},
		    {Backend::WebGPU, RequestWebGPU}
		};
#else
		static constexpr frozen::unordered_map<Backend, RequestFunction, 3u> requests{
		    {Backend::Vulkan, RequestVulkan},
		    {Backend::OpenGL, RequestOpenGL},
		    {Backend::WebGPU, RequestWebGPU}
		};
#endif // defined(_WIN32)

		auto request = requests.find(backend);
		if (request == requests.end()) {
			throw std::invalid_argument("Requested RHI backend is not available");
		}
		return request->second();
	}

} // namespace fyuu_rhi
