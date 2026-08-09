module;
#if !defined(__APPLE__)
#define VMA_IMPLEMENTATION
#include <vma/vk_mem_alloc.h>
#endif // !defined(__APPLE__)
module fyuu_rhi:vma_impl;
import :vulkan_traits;

namespace fyuu_rhi::vulkan {

	Backend::VMAAllocator::~VMAAllocator() noexcept {
		if (impl) {
			vmaDestroyAllocator(impl);
		}
	}
}