module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>

#include <utility>

#include <atomic>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#if defined(__clang__) && defined(_MSVC_STL_VERSION)
#define FYUU_RHI_USE_VULKAN_HEADER
#include <vulkan/vulkan_shared.hpp>
#endif
#include <vma/vk_mem_alloc.h>
#endif // !defined(__APPLE__)

module fyuu_rhi:vulkan_resource;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
#if !defined(FYUU_RHI_USE_VULKAN_HEADER)
import vulkan;
#endif
import :vulkan_traits;

namespace fyuu_rhi::vulkan {

	Backend::Resource::Buffer::Buffer(
		std::shared_ptr<VMAAllocator> const& mem_alloc_,
		VkBufferCreateInfo buf_info_,
		VkBuffer vk_handle_,
		VmaAllocation alloc_
	) : mem_alloc(mem_alloc_),
		buf_info(buf_info_),
		vk_handle(vk_handle_),
		alloc(alloc_),
		state(std::make_unique<ResourceState>()) {
	}

	Backend::Resource::Buffer::Buffer(Buffer&& other) noexcept
		: mem_alloc(std::move(other.mem_alloc)),
		buf_info(other.buf_info),
		vk_handle(std::exchange(other.vk_handle, nullptr)),
		alloc(std::exchange(other.alloc, nullptr)),
		state(std::move(other.state)) {
	}

	Backend::Resource::Buffer& Backend::Resource::Buffer::operator=(Buffer&& other) noexcept {
		if (this != &other) {
			if (mem_alloc && vk_handle && alloc) {
				vmaDestroyBuffer(mem_alloc->impl, vk_handle, alloc);
			}
			mem_alloc = std::move(other.mem_alloc);
			buf_info = other.buf_info;
			vk_handle = std::exchange(other.vk_handle, nullptr);
			alloc = std::exchange(other.alloc, nullptr);
			state = std::move(other.state);
		}
		return *this;
	}

	Backend::Resource::Buffer::~Buffer() noexcept {
		if (mem_alloc && vk_handle && alloc) {
			vmaDestroyBuffer(mem_alloc->impl, vk_handle, alloc);
		}
	}

	Backend::Resource::Texture::Texture(
		std::shared_ptr<VMAAllocator> const& mem_alloc_,
		VkImageCreateInfo tex_info_,
		VkImage vk_handle_,
		VmaAllocation alloc_
	) : mem_alloc(mem_alloc_),
		tex_info(tex_info_),
		vk_handle(vk_handle_),
		alloc(alloc_),
		state(std::make_unique<ResourceState>()) {
	}

	Backend::Resource::Texture::Texture(Texture&& other) noexcept
		: mem_alloc(std::move(other.mem_alloc)),
		tex_info(std::move(other.tex_info)),
		vk_handle(std::exchange(other.vk_handle, nullptr)),
		alloc(std::exchange(other.alloc, nullptr)),
		state(std::move(other.state)) {
	}

	Backend::Resource::Texture& Backend::Resource::Texture::operator=(Texture&& other) noexcept {
		if (this != &other) {
			if (mem_alloc && vk_handle && alloc) {
				vmaDestroyImage(mem_alloc->impl, vk_handle, alloc);
			}
			mem_alloc = std::move(other.mem_alloc);
			tex_info = std::move(other.tex_info);
			vk_handle = std::exchange(other.vk_handle, nullptr);
			alloc = std::exchange(other.alloc, nullptr);
			state = std::move(other.state);
		}
		return *this;
	}

	Backend::Resource::Texture::~Texture() noexcept {
		if (mem_alloc && vk_handle && alloc) {
			vmaDestroyImage(mem_alloc->impl, vk_handle, alloc);
		}
	}
}
#endif // !defined(__APPLE__)
