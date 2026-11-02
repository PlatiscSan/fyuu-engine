module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <stdexcept>
#include <utility>

#include <vector>

#include <optional>
#include <variant>

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
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :metal_data;
import :metal_instance;
#endif // defined(__APPLE__)
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
import :d3d12_data;
import :d3d12_instance;
import :opengl_instance_wgl;
#endif // defined(_WIN32)
#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :opengl_data;
import :vulkan_data;
import :vulkan_instance;
#endif // !defined(__APPLE__)
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
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
	/** @brief Inline native ownership and a stable public handle for one backend. */
	template <class Native, fyuu_rhi::Backend Type> struct InstanceSlot {
		std::optional<Native> native;
		fyuu_rhi::Instance instance{Type};
	};

	/** @brief Stores each native backend at its actual size instead of repeating the largest variant. */
	struct BackendInstances {
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
		InstanceSlot<fyuu_rhi::d3d12::Instance, fyuu_rhi::Backend::DirectX12> d3d12;
#endif // defined(_WIN32) && !defined(__EMSCRIPTEN__)
#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
		InstanceSlot<fyuu_rhi::vulkan::Instance, fyuu_rhi::Backend::Vulkan> vulkan;
#if defined(__linux__) && !defined(__ANDROID__)
		/// @brief Only OpenGL's mutually exclusive window-system implementations share storage.
		InstanceSlot<
		    std::variant<fyuu_rhi::opengl::EGLInstance, fyuu_rhi::opengl::GLXInstance>,
		    fyuu_rhi::Backend::OpenGL>
		    opengl;
#else
		InstanceSlot<fyuu_rhi::opengl::Instance, fyuu_rhi::Backend::OpenGL> opengl;
#endif // defined(__linux__) && !defined(__ANDROID__)
#elif defined(__APPLE__) && !defined(__EMSCRIPTEN__)
		InstanceSlot<fyuu_rhi::metal::Instance, fyuu_rhi::Backend::Metal> metal;
#endif // !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
		InstanceSlot<fyuu_rhi::webgpu::Instance, fyuu_rhi::Backend::WebGPU> webgpu;
	};

	/**
	 * @brief Suppresses native root destruction under DLL loader lock once for the whole registry.
	 * @note The active member is constructed normally. Explicit Vulkan refresh runs outside
	 * DLL teardown; other retained native roots remain alive until process reclamation.
	 */
	union InstanceStorage {
		BackendInstances instances;

		InstanceStorage() : instances{} {
		}

		~InstanceStorage() noexcept {
		}
	};

	InstanceStorage storage;

	/** @brief Creates a backend lazily without moving its stable handle or allocating a slot. */
	template <class Native, fyuu_rhi::Backend Type>
	fyuu_rhi::Instance& RequestSlot(InstanceSlot<Native, Type>* slot) {
		if (!slot->native) {
			slot->native.emplace(fyuu_rhi::CreateInstance<Native>{}());
		}
		return slot->instance;
	}

	/** @brief Passes the existing native address, or a typed null while its slot is retired. */
	template <class Native, fyuu_rhi::Backend Type, class Function>
	decltype(auto) VisitSlot(InstanceSlot<Native, Type> const* slot, Function&& Invoke) {
		return Invoke(slot->native ? std::addressof(*slot->native) : nullptr);
	}

	/** @brief Dispatches through the backend identity rather than an oversized owning variant. */
	template <class Function>
	decltype(auto) VisitInstance(fyuu_rhi::Backend backend, Function&& Invoke) {
		switch (backend) {
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
			case fyuu_rhi::Backend::DirectX12:
				return VisitSlot(&storage.instances.d3d12, Invoke);
#endif // defined(_WIN32) && !defined(__EMSCRIPTEN__)
#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
			case fyuu_rhi::Backend::Vulkan:
				return VisitSlot(&storage.instances.vulkan, Invoke);
			case fyuu_rhi::Backend::OpenGL:
#if defined(__linux__) && !defined(__ANDROID__)
				if (!storage.instances.opengl.native) {
					return Invoke(static_cast<fyuu_rhi::opengl::EGLInstance const*>(nullptr));
				}
				return std::visit(
				    [&Invoke](auto const& native) {
					    return Invoke(&native);
				    },
				    *storage.instances.opengl.native
				);
#else
				return VisitSlot(&storage.instances.opengl, Invoke);
#endif // defined(__linux__) && !defined(__ANDROID__)
#elif defined(__APPLE__) && !defined(__EMSCRIPTEN__)
			case fyuu_rhi::Backend::Metal:
				return VisitSlot(&storage.instances.metal, Invoke);
#endif // !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
			case fyuu_rhi::Backend::WebGPU:
				return VisitSlot(&storage.instances.webgpu, Invoke);
			default:
				throw std::invalid_argument("Requested RHI instance backend is not available");
		}
	}

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
	fyuu_rhi::Instance& RequestD3D12() {
		return RequestSlot(&storage.instances.d3d12);
	}
#endif // defined(_WIN32) && !defined(__EMSCRIPTEN__)

#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
	fyuu_rhi::Instance& RequestVulkan() {
		return RequestSlot(&storage.instances.vulkan);
	}
#elif defined(__APPLE__) && !defined(__EMSCRIPTEN__)
	fyuu_rhi::Instance& RequestMetal() {
		return RequestSlot(&storage.instances.metal);
	}
#endif // !defined(__APPLE__) && !defined(__EMSCRIPTEN__)

	fyuu_rhi::Instance& RequestWebGPU() {
#if defined(_WIN32) && !defined(NDEBUG) && !defined(__EMSCRIPTEN__)
		/// @brief Configure D3D12 debug validation before Dawn creates its first device.
		(void)RequestD3D12();
#endif // defined(_WIN32) && !defined(NDEBUG) && !defined(__EMSCRIPTEN__)
		return RequestSlot(&storage.instances.webgpu);
	}

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
	fyuu_rhi::Instance& RequestOpenGL() {
		if (!storage.instances.opengl.native) {
			if (fyuu_rhi::IsWayland()) {
				storage.instances.opengl.native.emplace(
				    std::in_place_type<fyuu_rhi::opengl::EGLInstance>,
				    fyuu_rhi::CreateInstance<fyuu_rhi::opengl::EGLInstance>{}()
				);
			} else {
				storage.instances.opengl.native.emplace(
				    std::in_place_type<fyuu_rhi::opengl::GLXInstance>,
				    fyuu_rhi::CreateInstance<fyuu_rhi::opengl::GLXInstance>{}()
				);
			}
		}
		return storage.instances.opengl.instance;
	}
#elif !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
	fyuu_rhi::Instance& RequestOpenGL() {
		return RequestSlot(&storage.instances.opengl);
	}
#endif // defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)

} // namespace

namespace fyuu_rhi {
	/**
	 * @brief Retires the Vulkan root so the next request uses the current context metadata.
	 * @note The host releases all Vulkan-derived objects before context reinitialization.
	 * Retired inline ownership releases messenger, instance and dispatcher in member order.
	 */
	void RefreshVulkanInstance() {
#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
		auto retired = std::move(storage.instances.vulkan.native);
		storage.instances.vulkan.native = std::nullopt;
#endif // !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
	}

	std::vector<PhysicalDevice> Instance::EnumeratePhysicalDevices() const {
		return VisitInstance(m_backend, []<class Native>(Native const* native) {
			if (!native) {
				return std::vector<PhysicalDevice>{};
			}
			return fyuu_rhi::EnumeratePhysicalDevices<Native>{native}();
		});
	}

	void Instance::ShareContextOnThisThread() {
		VisitInstance(m_backend, []<class Native>(Native const* native) {
			if (native) {
				ShareContextOnCurrentThread<Native>{native}();
			}
		});
	}

	std::span<Backend const> EnumerateBackends() noexcept {
		static constexpr Backend backends[]{
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
		    Backend::DirectX12,
#endif // defined(_WIN32)
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
		    Backend::Metal,
#elif !defined(__EMSCRIPTEN__)
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
#if defined(__EMSCRIPTEN__)
		static constexpr frozen::unordered_map<Backend, RequestFunction, 1u> requests{
		    {Backend::WebGPU, RequestWebGPU}
		};
#elif defined(_WIN32)
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
