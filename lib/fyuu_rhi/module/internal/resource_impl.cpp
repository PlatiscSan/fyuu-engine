module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <stdexcept>
#include <variant>

#include <span>
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
	void ResourceMapScope::Reset() noexcept {
		if (m_unmap) {
			m_unmap(m_context, ResourceDataRange{m_offset, m_size}, m_writable);
		}
		m_context = nullptr;
		m_data = nullptr;
		m_offset = 0u;
		m_size = 0u;
		m_writable = false;
		m_unmap = nullptr;
	}

	ResourceMapScope::ResourceMapScope(ResourceMapScope&& other) noexcept :
	    m_context(std::exchange(other.m_context, nullptr)),
	    m_data(std::exchange(other.m_data, nullptr)), m_offset(std::exchange(other.m_offset, 0u)),
	    m_size(std::exchange(other.m_size, 0u)), m_writable(std::exchange(other.m_writable, false)),
	    m_unmap(std::exchange(other.m_unmap, nullptr)) {
	}

	ResourceMapScope& ResourceMapScope::operator=(ResourceMapScope&& other) noexcept {
		if (this != &other) {
			Reset();
			m_context = std::exchange(other.m_context, nullptr);
			m_data = std::exchange(other.m_data, nullptr);
			m_offset = std::exchange(other.m_offset, 0u);
			m_size = std::exchange(other.m_size, 0u);
			m_writable = std::exchange(other.m_writable, false);
			m_unmap = std::exchange(other.m_unmap, nullptr);
		}
		return *this;
	}

	ResourceMapScope::~ResourceMapScope() noexcept {
		Reset();
	}

	void ResourceMapScope::Write(std::span<std::byte const> data) {
		if (!m_writable) {
			throw std::logic_error("A DeviceReadback mapping is read-only");
		}
		if (data.size() > m_size) {
			throw std::out_of_range("The write exceeds the mapped interval");
		}
		std::ranges::copy(data, m_data);
	}

	View Resource::CreateBufferView(
	    std::size_t offset,
	    std::size_t range,
	    ResourceFlags const& flags
	) {
		if (!m_impl) {
			throw std::runtime_error("Cannot create a view from an empty resource");
		}
		return std::visit(
		    [&]<class NativeResource>(NativeResource& native) {
			    return fyuu_rhi::CreateBufferView<NativeResource>{&native}(offset, range, flags);
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
			    return fyuu_rhi::CreateTextureView<NativeResource>{
			        &native
			    }(base_mip_lvl, mip_lvl_cnt, base_arr_layer, arr_layer_cnt, flags);
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

	ResourceMapScope Resource::Map(ResourceDataRange range) {
		using ResourceBits = ResourceFlagBits;

		if (!m_impl) {
			throw std::runtime_error("Cannot map an empty resource");
		}
		auto size = std::get_if<std::size_t>(&m_impl->size_or_extent);
		if (!size) {
			throw std::invalid_argument("Textures cannot be mapped directly");
		}
		auto const readable = m_impl->flags.Test(ResourceBits::DeviceReadback);
		auto const writable = m_impl->flags.Test(ResourceBits::HostVisible);
		if (!readable && !writable) {
			throw std::invalid_argument("A mapped buffer must be DeviceReadback or HostVisible");
		}
		if (range.offset > *size) {
			throw std::out_of_range("The mapped offset exceeds the buffer size");
		}
		if (range.size == 0u) {
			range.size = *size - range.offset;
		}
		if (range.size > *size - range.offset) {
			throw std::out_of_range("The mapped range exceeds the buffer size");
		}
		return std::visit(
		    [&]<class NativeResource>(NativeResource& native) {
			    return MapResource<NativeResource>{&native}(range, writable && !readable);
		    },
		    m_impl->native
		);
	}

} // namespace fyuu_rhi
