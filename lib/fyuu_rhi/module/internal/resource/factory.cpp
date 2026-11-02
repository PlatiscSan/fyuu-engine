module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <stdexcept>
#include <utility>

#include <cmath>

#include <variant>
#endif // !defined(__cpp_lib_modules)

#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#include <boost/scope/unique_resource.hpp>
#endif // !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <D3D12MemAlloc.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#endif // defined(_WIN32) && !defined(__EMSCRIPTEN__)

module fyuu_rhi:resource_factory;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource;
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :metal_data;
#endif // defined(__APPLE__)
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
import :d3d12_data;
#endif // defined(_WIN32)
#if !defined(__APPLE__) && !defined(__EMSCRIPTEN__)
import :opengl_data;
import :vulkan_data;
#endif // !defined(__APPLE__)
import :webgpu_data;

namespace fyuu_rhi {
	/** @brief Validates and resolves attachment defaults without initializing GPU storage. */
	TextureClearValue ResolveTextureClearValue(ResourceFlags const& flags, TextureClearValue const& value) {
		using Bits = ResourceFlagBits;
		if (!flags.Test(Bits::RenderAttachment) && !flags.Test(Bits::TransientAttachment)) {
			if (!std::holds_alternative<std::monostate>(value)) {
				throw std::invalid_argument("A clear default requires an attachment texture");
			}
			return {};
		}
		bool depth = flags.Test(Bits::D16Unorm) || flags.Test(Bits::D24UnormS8Uint) ||
			flags.Test(Bits::D32Float) || flags.Test(Bits::D32FloatS8X24Uint);
		auto resolved = std::holds_alternative<std::monostate>(value) ?
			(depth ? TextureClearValue{DepthStencilClearValue{}} : TextureClearValue{ColorClearValue{}}) : value;
		if (depth) {
			auto clear = std::get_if<DepthStencilClearValue>(&resolved);
			if (!clear || !std::isfinite(clear->depth) || clear->depth < 0.0f ||
				clear->depth > 1.0f || clear->stencil > 255u) {
				throw std::invalid_argument("Invalid depth/stencil attachment clear default");
			}
		} else {
			auto clear = std::get_if<ColorClearValue>(&resolved);
			if (!clear || !std::isfinite(clear->red) || !std::isfinite(clear->green) ||
				!std::isfinite(clear->blue) || !std::isfinite(clear->alpha)) {
				throw std::invalid_argument("Invalid color attachment clear default");
			}
		}
		return resolved;
	}

	struct ResourceImplementation {
		std::variant<std::size_t, ResourceTextureExtent> size_or_extent;
		ResourceFlags flags;
		std::variant<
			std::monostate,
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
			d3d12::Resource,
#endif // defined(_WIN32)
#if defined(__APPLE__) && !defined(__EMSCRIPTEN__)
			metal::Resource,
#elif !defined(__EMSCRIPTEN__)
			vulkan::Resource,
			opengl::Resource,
#endif // !defined(__APPLE__)
			webgpu::Resource
		> native;
	};

	/** @brief Emits inline-storage lifecycle operations where native template definitions are available. */
	template class details::FastPimpl<ResourceImplementation, 192u>;

	template <class NativeResource>
	Resource MakeResource(NativeResource&& native, std::size_t size, ResourceFlags const& flags) {
		return Resource(
			Resource::ImplementationStorage(
				std::in_place, size, flags, std::forward<NativeResource>(native)
			)
		);
	}

	template <class NativeResource>
	Resource MakeResource(
		NativeResource&& native,
		ResourceTextureExtent const& extent,
		ResourceFlags const& flags,
		TextureClearValue const& clear_value = {}
	) {
		native.default_clear_value = ResolveTextureClearValue(flags, clear_value);
		return Resource(
			Resource::ImplementationStorage(
				std::in_place, extent, flags, std::forward<NativeResource>(native)
			)
		);
	}

} // namespace fyuu_rhi
