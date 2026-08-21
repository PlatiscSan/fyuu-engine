module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>

#include <cstdint>

#include <variant>
#endif // !defined(__cpp_lib_modules)
#include <dawn/webgpu_cpp.h>

module fyuu_rhi:webgpu_resource;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource_dispatch;
import :view_factory;
import :webgpu_data;
import :webgpu_utility;

namespace fyuu_rhi {

	template <>
	struct CreateBufferView<webgpu::Resource> {
		webgpu::Resource* resource;

		View operator()(std::size_t offset, std::size_t range, ResourceFlags const& flags) const {
			(void)flags;
			auto const& buffer = std::get<wgpu::Buffer>(resource->impl);
			auto size = buffer.GetSize();
			if (offset > size || range > size - offset) {
				throw std::out_of_range(
					"A WebGPU buffer view exceeds the source buffer"
				);
			}
			// WebGPU has no buffer-view object; a view is just a (buffer, offset,
			// size) window applied at bind time.
			return MakeView(webgpu::View{ webgpu::View::Buffer{ buffer, offset, range } });
		}
	};

	template <>
	struct CreateTextureView<webgpu::Resource> {
		webgpu::Resource* resource;

		View operator()(
			std::size_t base_mip_lvl,
			std::size_t mip_lvl_cnt,
			std::size_t base_arr_layer,
			std::size_t arr_layer_cnt,
			ResourceFlags const& flags
		) const {
			auto const& tex = std::get<wgpu::Texture>(resource->impl);
			wgpu::TextureViewDescriptor view_desc = {
				.format = webgpu::ResourceFormat(flags),
				.dimension = webgpu::TextureViewDimension(flags),
				.baseMipLevel = static_cast<std::uint32_t>(base_mip_lvl),
				.mipLevelCount = static_cast<std::uint32_t>(mip_lvl_cnt),
				.baseArrayLayer = static_cast<std::uint32_t>(base_arr_layer),
				.arrayLayerCount = static_cast<std::uint32_t>(arr_layer_cnt),
				.aspect = webgpu::TextureViewAspect(flags)
			};
			return MakeView(webgpu::View{ tex.CreateView(&view_desc) });
		}
	};

} // namespace fyuu_rhi
