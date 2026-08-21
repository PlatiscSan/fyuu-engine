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
import :resource;
import :resource_dispatch;
import :resource_factory;
import :view;
#if defined(__APPLE__)
import :metal_resource;
#endif // defined(__APPLE__)
#if defined(_WIN32)
import :d3d12_resource;
#endif // defined(_WIN32)
#if !defined(__APPLE__)
import :opengl_resource;
import :vulkan_resource;
#endif // !defined(__APPLE__)
import :webgpu_resource;

namespace fyuu_rhi {

	View Resource::CreateBufferView(std::size_t offset, std::size_t range, ResourceFlags const& flags) {
		if (!m_impl) {
			throw std::runtime_error("Cannot create a view from an empty resource");
		}
		return std::visit(
			[&]<class NativeResource>(NativeResource& native) {
				return fyuu_rhi::CreateBufferView<NativeResource>{ &native }(offset, range, flags);
			},
			m_impl->native
		);
	}

	View Resource::CreateTextureView(
		std::size_t base_mip_lvl,
		std::size_t mip_lvl_cnt,
		std::size_t base_arr_layer,
		std::size_t arr_layer_cnt,
		ResourceFlags const& flags
	) {
		if (!m_impl) {
			throw std::runtime_error("Cannot create a view from an empty resource");
		}
		return std::visit(
			[&]<class NativeResource>(NativeResource& native) {
				return fyuu_rhi::CreateTextureView<NativeResource>{ &native }(
					base_mip_lvl,
					mip_lvl_cnt,
					base_arr_layer,
					arr_layer_cnt,
					flags
				);
			},
			m_impl->native
		);
	}

	std::size_t Resource::GetBufferSize() const {
		if (!m_impl) {
			throw std::runtime_error("Cannot query an empty resource");
		}
		return std::get<std::size_t>(m_impl->size_or_extent);
	}

	ResourceFlags Resource::GetFlags() const noexcept {
		if (!m_impl) {
			return {};
		}
		return m_impl->flags;
	}

	ResourceTextureExtent Resource::GetTextureExtent() const {
		if (!m_impl) {
			throw std::runtime_error("Cannot query an empty resource");
		}
		return std::get<ResourceTextureExtent>(m_impl->size_or_extent);
	}

} // namespace fyuu_rhi
