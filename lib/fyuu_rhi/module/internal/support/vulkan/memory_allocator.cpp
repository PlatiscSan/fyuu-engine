module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>

#include <variant>
#include <span>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#if defined(__clang__) && defined(_MSVC_STL_VERSION)
#define FYUU_RHI_USE_VULKAN_HEADER
#include <vulkan/vulkan_shared.hpp>
#endif // defined(__clang__) && defined(_MSVC_STL_VERSION)
#include <vma/vk_mem_alloc.h>
#endif // !defined(__APPLE__)

module fyuu_rhi:vulkan_memory_allocator;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
#if !defined(FYUU_RHI_USE_VULKAN_HEADER)
import vulkan;
#endif // !defined(FYUU_RHI_USE_VULKAN_HEADER)
import :resource;

namespace fyuu_rhi::vulkan {

	struct MemoryAllocatorState;

	/// Move-only ownership returned by MemoryAllocator. It keeps the allocator
	/// alive and destroys the paired Vulkan object and VMA allocation together.
	class ManagedAllocation final {
		friend class MemoryAllocator;

	private:
		std::shared_ptr<MemoryAllocatorState> m_allocator;
		std::variant<VkBuffer, VkImage> m_resource;
		VmaAllocation m_allocation;

		ManagedAllocation(std::shared_ptr<MemoryAllocatorState> const& allocator, VkBuffer resource, VmaAllocation allocation) noexcept;

		ManagedAllocation(std::shared_ptr<MemoryAllocatorState> const& allocator, VkImage resource, VmaAllocation allocation) noexcept;

		void Destroy() noexcept;

	public:
		ManagedAllocation(ManagedAllocation const&) = delete;
		ManagedAllocation& operator=(ManagedAllocation const&) = delete;

		ManagedAllocation(ManagedAllocation&& other) noexcept;

		ManagedAllocation& operator=(ManagedAllocation&& other) noexcept;

		~ManagedAllocation() noexcept;

		VkBuffer GetBuffer() const noexcept;

		VkImage GetImage() const noexcept;

		vk::SharedDevice GetLogicalDevice() const noexcept;

		void Write(std::size_t offset, std::span<std::byte const> data) const;
	};

	/// Copyable allocator handle. Every copy and every outstanding allocation
	/// shares the same VMA allocator and Vulkan device lifetime.
	class MemoryAllocator final {
	private:
		std::shared_ptr<MemoryAllocatorState> m_impl;

	public:
		MemoryAllocator(vk::SharedDevice const& device, VmaAllocator impl);

		ManagedAllocation AllocateBuffer(vk::BufferCreateInfo const& resource_info, ResourceFlags const& flags) const;

		ManagedAllocation AllocateImage(vk::ImageCreateInfo const& resource_info, ResourceFlags const& flags) const;
	};

} // namespace fyuu_rhi::vulkan
#endif // !defined(__APPLE__)
