module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <utility>
#include <variant>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:resource_factory;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource;
#if defined(__APPLE__)
import :metal_data;
#endif // defined(__APPLE__)
#if defined(_WIN32)
import :d3d12_data;
#endif // defined(_WIN32)
#if !defined(__APPLE__)
import :opengl_data;
import :vulkan_data;
#endif // !defined(__APPLE__)
import :webgpu_data;

namespace fyuu_rhi {

	struct ResourceImplementation {
		std::variant<std::size_t, ResourceTextureExtent> size_or_extent;
		ResourceFlags flags;
		std::variant<
			std::monostate,
#if defined(_WIN32)
			d3d12::Resource,
#endif // defined(_WIN32)
#if defined(__APPLE__)
			metal::Resource,
#else
			vulkan::Resource,
			opengl::Resource,
#endif // !defined(__APPLE__)
			webgpu::Resource
		> native;
	};

	template <class NativeResource>
	Resource MakeResource(NativeResource&& native, std::size_t size, ResourceFlags const& flags) {
		return Resource(
			Resource::UniqueHandle(
				new ResourceImplementation{ size, flags, std::forward<NativeResource>(native) },
				[](ResourceImplementation* implementation) noexcept {
					delete implementation;
				}
			)
		);
	}

	template <class NativeResource>
	Resource MakeResource(NativeResource&& native, ResourceTextureExtent const& extent, ResourceFlags const& flags) {
		return Resource(
			Resource::UniqueHandle(
				new ResourceImplementation{ extent, flags, std::forward<NativeResource>(native) },
				[](ResourceImplementation* implementation) noexcept {
					delete implementation;
				}
			)
		);
	}

} // namespace fyuu_rhi
