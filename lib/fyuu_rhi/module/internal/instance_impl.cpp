module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <functional>

#include <span>
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

	template <fyuu_rhi::Backend backend, class Native>
	void Request(std::function<void(fyuu_rhi::Instance)> const& func) {
		static Native native = fyuu_rhi::CreateInstance<Native>{}();
		static fyuu_rhi::InstanceImplementation impl{ backend, &native };
		func(fyuu_rhi::Instance{ &impl });
	}

#if defined(__linux__) && !defined(__ANDROID__)
	void RequestOpenGL(std::function<void(fyuu_rhi::Instance)> const& func) {
		fyuu_rhi::IsWayland() ?
			Request<fyuu_rhi::Backend::OpenGL, fyuu_rhi::opengl::EGLInstance>(func) :
			Request<fyuu_rhi::Backend::OpenGL, fyuu_rhi::opengl::GLXInstance>(func);
	}
#endif // defined(__linux__) && !defined(__ANDROID__)

} // namespace

namespace fyuu_rhi {
	std::vector<PhysicalDevice> Instance::EnumeratePhysicalDevices() const {
		switch (m_impl->type) {
#if defined(_WIN32)
		case Backend::DirectX12:
			return fyuu_rhi::EnumeratePhysicalDevices<d3d12::Instance>{ static_cast<d3d12::Instance const*>(m_impl->native) }();
#endif // defined(_WIN32)
#if defined(__APPLE__)		
		case Backend::Metal:
			return fyuu_rhi::EnumeratePhysicalDevices<metal::Instance>{ static_cast<metal::Instance const*>(m_impl->native) }();
#else
		case Backend::Vulkan:
			return fyuu_rhi::EnumeratePhysicalDevices<vulkan::Instance>{ static_cast<vulkan::Instance const*>(m_impl->native) }();
		case Backend::OpenGL:
#if defined(_WIN32)
			return fyuu_rhi::EnumeratePhysicalDevices<opengl::Instance>{ static_cast<opengl::Instance const*>(m_impl->native) }();
#elif defined(__linux__) && !defined(__ANDROID__)
			return IsWayland() ? 
				fyuu_rhi::EnumeratePhysicalDevices<opengl::EGLInstance>{ static_cast<opengl::EGLInstance const*>(m_impl->native) }() :
				fyuu_rhi::EnumeratePhysicalDevices<opengl::GLXInstance>{ static_cast<opengl::GLXInstance const*>(m_impl->native) }();
#elif defined(__ANDROID__)
			return fyuu_rhi::EnumeratePhysicalDevices<opengl::Instance>{ static_cast<opengl::Instance const*>(m_impl->native) }();
#endif // defined(_WIN32)
#endif // defined(__APPLE__)
		case Backend::WebGPU:
			return fyuu_rhi::EnumeratePhysicalDevices<webgpu::Instance>{ static_cast<webgpu::Instance const*>(m_impl->native) }();
		default:
			return {};
		}
	}

	void Instance::ShareContextOnThisThread() {
		if (m_impl->type != Backend::OpenGL) {
			return;
		}
#if defined(_WIN32)
		ShareContextOnCurrentThread<opengl::Instance>{ static_cast<opengl::Instance const*>(m_impl->native) }();
#elif defined(__linux__) && !defined(__ANDROID__)
		if (IsWayland()) {
			ShareContextOnCurrentThread<opengl::EGLInstance>{ static_cast<opengl::EGLInstance const*>(m_impl->native) }();
		}
		else {
			ShareContextOnCurrentThread<opengl::GLXInstance>{ static_cast<opengl::GLXInstance const*>(m_impl->native) }();
		}
#elif defined(__ANDROID__)
		ShareContextOnCurrentThread<opengl::Instance>{ static_cast<opengl::Instance const*>(m_impl->native) }();
#endif // defined(_WIN32)
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

	void RequestInstance(Backend backend, std::function<void(Instance)> const& func) {
		if (!IsInitialized()) {
			throw std::runtime_error("RHI context is not initialized yet");
		}
		if (!func) {
			throw std::invalid_argument("RequestInstance requires a callback");
		}

		using RequestFunction = void(*)(std::function<void(fyuu_rhi::Instance)> const&);
#if defined(_WIN32)
		static constexpr frozen::unordered_map<Backend, RequestFunction, 4u> requests{
			{ Backend::DirectX12, Request<Backend::DirectX12, d3d12::Instance> },
			{ Backend::Vulkan, Request<Backend::Vulkan, vulkan::Instance> },
			{ Backend::OpenGL, Request<Backend::OpenGL, opengl::Instance> },
			{ Backend::WebGPU, Request<Backend::WebGPU, webgpu::Instance> }
		};
#elif defined(__APPLE__)
		static constexpr frozen::unordered_map<Backend, RequestFunction, 2u> requests{
			{ Backend::Metal, Request<Backend::Metal, metal::Instance> },
			{ Backend::WebGPU, Request<Backend::WebGPU, webgpu::Instance> }
		};
#elif defined(__linux__) && !defined(__ANDROID__)
		static constexpr frozen::unordered_map<Backend, RequestFunction, 3u> requests{
			{ Backend::Vulkan, Request<Backend::Vulkan, vulkan::Instance> },
			{ Backend::OpenGL, RequestOpenGL },
			{ Backend::WebGPU, Request<Backend::WebGPU, webgpu::Instance> }
		};
#else
		static constexpr frozen::unordered_map<Backend, RequestFunction, 3u> requests{
			{ Backend::Vulkan, Request<Backend::Vulkan, vulkan::Instance> },
			{ Backend::OpenGL, Request<Backend::OpenGL, opengl::Instance> },
			{ Backend::WebGPU, Request<Backend::WebGPU, webgpu::Instance> }
		};
#endif // defined(_WIN32)

		auto request = requests.find(backend);
		if (request == requests.end()) {
			throw std::invalid_argument("Requested RHI backend is not available");
		}
		request->second(func);
	}

} // namespace fyuu_rhi
