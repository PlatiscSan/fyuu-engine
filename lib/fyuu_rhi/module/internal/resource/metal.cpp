module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <utility>
#endif // !defined(__cpp_lib_modules)
#if defined(__APPLE__)
#include <Metal/Metal.hpp>
#endif // defined(__APPLE__)

module fyuu_rhi:metal_resource;
#if defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :metal_data;
import :metal_utility;
import :resource_dispatch;
import :view_factory;

namespace fyuu_rhi {

	template <>
	struct CreateBufferView<metal::Resource> {
		metal::Resource* resource;

		View operator()(std::size_t offset, std::size_t range, ResourceFlags const& flags) const {
			(void)flags;
			auto buffer = std::get_if<NS::SharedPtr<MTL::Buffer>>(&resource->impl);
			if (!buffer) {
				throw std::invalid_argument("A Metal buffer view requires a buffer resource");
			}
			auto length = (*buffer)->length();
			if (offset > length || range > length - offset) {
				throw std::out_of_range("A Metal buffer view exceeds the source buffer");
			}
			return MakeView(
				metal::View{ metal::BufferView{ *buffer, offset, range } }
			);
		}
	};

	template <>
	struct CreateTextureView<metal::Resource> {
		metal::Resource* resource;

		View operator()(
			std::size_t base_mip_lvl,
			std::size_t mip_lvl_cnt,
			std::size_t base_arr_layer,
			std::size_t arr_layer_cnt,
			ResourceFlags const& flags
		) const {
			auto texture = std::get_if<NS::SharedPtr<MTL::Texture>>(&resource->impl);
			if (!texture) {
				throw std::invalid_argument("A Metal texture view requires a texture resource");
			}
			// When no explicit view type is requested, derive it from the parent
			// texture so the view stays compatible with the parent's layout.
			auto texture_type = metal::ViewTextureType(flags).value_or((*texture)->textureType());
			auto view = NS::TransferPtr(
				(*texture)->newTextureViewWithPixelFormat(
					metal::PixelFormat(flags),
					texture_type,
					NS::Range{ base_mip_lvl, mip_lvl_cnt },
					NS::Range{ base_arr_layer, arr_layer_cnt }
				)
			);
			return MakeView(metal::View{ std::move(view) });
		}
	};

} // namespace fyuu_rhi
#endif // defined(__APPLE__)
