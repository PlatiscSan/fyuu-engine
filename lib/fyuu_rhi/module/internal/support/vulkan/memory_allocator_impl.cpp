module;
// This dedicated implementation partition works around an IFC import bug in
// MSVC 14.51.36231 (CL 19.51.36231). Keeping VMA_IMPLEMENTATION and the
// MemoryAllocator function bodies in vulkan_memory_allocator made MSVC fail
// while importing std from vulkan_resource with C1116. Isolating them here lets
// vulkan_resource import std without deserializing the VMA implementation as
// part of the vulkan_memory_allocator interface.
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstring>

#include <memory>
#include <stdexcept>
#include <utility>

#include <variant>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#if defined(__clang__) && defined(_MSVC_STL_VERSION)
#define FYUU_RHI_USE_VULKAN_HEADER
#include <vulkan/vulkan_shared.hpp>
#endif // defined(__clang__) && defined(_MSVC_STL_VERSION)
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>
#endif // !defined(__APPLE__)

module fyuu_rhi:vulkan_memory_allocator_impl;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
#if !defined(FYUU_RHI_USE_VULKAN_HEADER)
import vulkan;
#endif // !defined(FYUU_RHI_USE_VULKAN_HEADER)
import :vulkan_memory_allocator;

namespace {

	VmaAllocationCreateInfo AllocationInfo(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		if (flags.TestMultipleInRange(Bits::UndedicatedAllocation, Bits::DedicatedAllocation)) {
			throw std::invalid_argument(
				"A Vulkan allocation cannot be both undedicated and dedicated"
			);
		}
		if (flags.TestMultipleInRange(Bits::MinOffsetAllocation, Bits::FirstFitAllocation)) {
			throw std::invalid_argument(
				"A Vulkan allocation requires at most one placement strategy"
			);
		}
		if (flags.TestMultipleInRange(Bits::DeviceLocal, Bits::DeviceReadback)) {
			throw std::invalid_argument(
				"A Vulkan allocation requires at most one memory access policy"
			);
		}

		VmaAllocationCreateInfo result{};
		result.usage = flags.Test(Bits::DeviceLocal) ?
			VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE :
			VMA_MEMORY_USAGE_AUTO;
		if (flags.Test(Bits::UndedicatedAllocation)) {
			result.flags |= VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT;
		}
		if (flags.Test(Bits::DedicatedAllocation)) {
			result.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		}
		if (flags.Test(Bits::AllocationWithinBudget)) {
			result.flags |= VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
		}
		if (flags.Test(Bits::AllocationAtUpperAddress)) {
			result.flags |= VMA_ALLOCATION_CREATE_UPPER_ADDRESS_BIT;
		}
		if (flags.Test(Bits::AllocationAliasAllowed)) {
			result.flags |= VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
		}
		if (flags.Test(Bits::MinOffsetAllocation)) {
			result.flags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_OFFSET_BIT;
		}
		if (flags.Test(Bits::BestFitAllocation)) {
			result.flags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
		}
		if (flags.Test(Bits::FirstFitAllocation)) {
			result.flags |= VMA_ALLOCATION_CREATE_STRATEGY_MIN_TIME_BIT;
		}
		if (flags.Test(Bits::HostVisible)) {
			result.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		}
		if (flags.Test(Bits::DeviceReadback)) {
			result.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
		}
		return result;
	}


} // namespace

namespace fyuu_rhi::vulkan {

	struct MemoryAllocatorState final {
		vk::SharedDevice logical_device;
		VmaAllocator impl;

		~MemoryAllocatorState() noexcept {
			if (impl) {
				vmaDestroyAllocator(impl);
			}
		}
	};

	ManagedAllocation::ManagedAllocation(
		std::shared_ptr<MemoryAllocatorState> const& allocator,
		VkBuffer resource,
		VmaAllocation allocation
	) noexcept
		: m_allocator(allocator),
		m_resource(resource),
		m_allocation(allocation) {
	}

	ManagedAllocation::ManagedAllocation(
		std::shared_ptr<MemoryAllocatorState> const& allocator,
		VkImage resource,
		VmaAllocation allocation
	) noexcept
		: m_allocator(allocator),
		m_resource(resource),
		m_allocation(allocation) {
	}

	void ManagedAllocation::Destroy() noexcept {
		if (!m_allocator || !m_allocation) {
			return;
		}
		if (auto buffer = std::get_if<VkBuffer>(&m_resource)) {
			if (*buffer) {
				vmaDestroyBuffer(
					m_allocator->impl,
					*buffer,
					m_allocation
				);
			}
		}
		else {
			auto image = std::get<VkImage>(m_resource);
			if (image) {
				vmaDestroyImage(
					m_allocator->impl,
					image,
					m_allocation
				);
			}
		}
		m_allocation = nullptr;
	}

	ManagedAllocation::ManagedAllocation(ManagedAllocation&& other) noexcept
		: m_allocator(std::move(other.m_allocator)),
		m_resource(std::move(other.m_resource)),
		m_allocation(std::exchange(other.m_allocation, nullptr)) {
	}

	ManagedAllocation& ManagedAllocation::operator=(ManagedAllocation&& other) noexcept {
		if (this != &other) {
			Destroy();
			m_allocator = std::move(other.m_allocator);
			m_resource = std::move(other.m_resource);
			m_allocation = std::exchange(other.m_allocation, nullptr);
		}
		return *this;
	}

	ManagedAllocation::~ManagedAllocation() noexcept {
		Destroy();
	}

	VkBuffer ManagedAllocation::GetBuffer() const noexcept {
		if (auto result = std::get_if<VkBuffer>(&m_resource)) {
			return *result;
		}
		return nullptr;
	}

	VkImage ManagedAllocation::GetImage() const noexcept {
		if (auto result = std::get_if<VkImage>(&m_resource)) {
			return *result;
		}
		return nullptr;
	}

	vk::SharedDevice fyuu_rhi::vulkan::ManagedAllocation::GetLogicalDevice() const noexcept {
		return m_allocator->logical_device;
	}

	void ManagedAllocation::Write(std::size_t offset, std::span<std::byte const> data) const {
		void* destination = nullptr;
		auto result = vmaMapMemory(
			m_allocator->impl,
			m_allocation,
			&destination
		);
		if (result != VK_SUCCESS) {
			throw vk::SystemError(
				static_cast<vk::Result>(result),
				"Failed to map a Vulkan allocation"
			);
		}
		try {
			std::memcpy(
				static_cast<std::byte*>(destination) + offset,
				data.data(),
				data.size()
			);
			vmaFlushAllocation(
				m_allocator->impl,
				m_allocation,
				offset,
				data.size()
			);
		}
		catch (...) {
			vmaUnmapMemory(
				m_allocator->impl,
				m_allocation
			);
			throw;
		}
		vmaUnmapMemory(
			m_allocator->impl,
			m_allocation
		);
	}

	MemoryAllocator::MemoryAllocator(vk::SharedDevice const& logical_device, VmaAllocator impl)
		: m_impl(std::make_shared<MemoryAllocatorState>(logical_device, impl)) {
	}

	ManagedAllocation MemoryAllocator::AllocateBuffer(vk::BufferCreateInfo const& resource_info, ResourceFlags const& flags) const {
		auto native_resource_info = VkBufferCreateInfo(resource_info);
		auto allocation_info = AllocationInfo(flags);
		VkBuffer resource = nullptr;
		VmaAllocation allocation = nullptr;
		auto result = vmaCreateBuffer(
			m_impl->impl,
			&native_resource_info,
			&allocation_info,
			&resource,
			&allocation,
			nullptr
		);
		if (result != VK_SUCCESS) {
			throw vk::SystemError(
				static_cast<vk::Result>(result),
				"Failed to allocate a Vulkan buffer"
			);
		}
		return ManagedAllocation(m_impl, resource, allocation);
	}

	ManagedAllocation MemoryAllocator::AllocateImage(vk::ImageCreateInfo const& resource_info, ResourceFlags const& flags) const {
		auto native_resource_info = VkImageCreateInfo(resource_info);
		auto allocation_info = AllocationInfo(flags);
		VkImage resource = nullptr;
		VmaAllocation allocation = nullptr;
		auto result = vmaCreateImage(
			m_impl->impl,
			&native_resource_info,
			&allocation_info,
			&resource,
			&allocation,
			nullptr
		);
		if (result != VK_SUCCESS) {
			throw vk::SystemError(
				static_cast<vk::Result>(result),
				"Failed to allocate a Vulkan image"
			);
		}
		return ManagedAllocation(m_impl, resource, allocation);
	}

} // namespace fyuu_rhi::vulkan
#endif // !defined(__APPLE__)
