module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <utility>

#include <cstdint>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)
#define FYUU_RHI_USE_VULKAN_HEADER
#include <vulkan/vulkan_shared.hpp>
#endif // !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)

module fyuu_rhi:vulkan_resource;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
#if !defined(FYUU_RHI_USE_VULKAN_HEADER)
import vulkan;
#endif // !defined(FYUU_RHI_USE_VULKAN_HEADER)
import :resource_dispatch;
import :view_factory;
import :vulkan_data;
import :vulkan_memory_allocator;
import :vulkan_utility;

namespace fyuu_rhi {

	template <>
	struct CreateBufferView<vulkan::Resource> {
		vulkan::Resource* resource;

		View operator()(std::size_t offset, std::size_t range, ResourceFlags const& flags) const {
			auto device = resource->allocation.GetLogicalDevice();
			vk::Buffer buf = resource->allocation.GetBuffer();
			auto format = vulkan::ResourceFormat(flags);
			vk::BufferViewCreateInfo info{
				{},
				buf,
				format,
				offset,
				range
			};
			vk::SharedBufferView shared_view(
				device->createBufferView(info, nullptr, *resource->dispatcher),
				device,
				{ nullptr, *resource->dispatcher }
			);
			vulkan::View result;
			result.impl = std::move(shared_view);
			result.format = format;
			return MakeView(std::move(result));
		}
	};

	template <>
	struct CreateTextureView<vulkan::Resource> {
		vulkan::Resource* resource;

		View operator()(
			std::size_t base_mip_lvl,
			std::size_t mip_lvl_cnt,
			std::size_t base_arr_layer,
			std::size_t arr_layer_cnt,
			ResourceFlags const& flags
		) const {
			auto device = resource->allocation.GetLogicalDevice();
			vk::Image tex = resource->allocation.GetImage();
			auto format = vulkan::ResourceFormat(flags);
			vk::ImageSubresourceRange subresource_range{
				vulkan::ImageAspect(flags, format),
				static_cast<std::uint32_t>(base_mip_lvl),
				static_cast<std::uint32_t>(mip_lvl_cnt),
				static_cast<std::uint32_t>(base_arr_layer),
				static_cast<std::uint32_t>(arr_layer_cnt)
			};
			vk::ImageViewCreateInfo info{
				{},
				tex,
				vulkan::ImageViewType(flags),
				format,
				{
					vk::ComponentSwizzle::eIdentity,
					vk::ComponentSwizzle::eIdentity,
					vk::ComponentSwizzle::eIdentity,
					vk::ComponentSwizzle::eIdentity
				},
				subresource_range
			};
			vk::SharedImageView shared_view(
				device->createImageView(info, nullptr, *resource->dispatcher),
				device,
				{ nullptr, *resource->dispatcher }
			);
			vulkan::View result;
			result.impl = std::move(shared_view);
			result.format = format;
			result.subresource_range = subresource_range;
			return MakeView(std::move(result));
		}
	};

} // namespace fyuu_rhi
#endif // !defined(__APPLE__)
