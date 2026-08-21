module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#endif // !defined(__cpp_lib_modules)

module fyuu_rhi:resource_dispatch;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource;
import :view;

namespace fyuu_rhi {

	template <class NativeResource>
	struct CreateBufferView {
		NativeResource* resource;

		View operator()(
			std::size_t offset,
			std::size_t range,
			ResourceFlags const& flags
		) const {
			throw std::runtime_error(
				"Buffer view creation is not implemented for this backend"
			);
		}
	};

	template <class NativeResource>
	struct CreateTextureView {
		NativeResource* resource;

		View operator()(
			std::size_t base_mip_lvl,
			std::size_t mip_lvl_cnt,
			std::size_t base_arr_layer,
			std::size_t arr_layer_cnt,
			ResourceFlags const& flags
		) const {
			throw std::runtime_error(
				"Texture view creation is not implemented for this backend"
			);
		}
	};

} // namespace fyuu_rhi
