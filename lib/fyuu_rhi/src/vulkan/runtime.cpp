#include <version>

#if defined(__clang__) && defined(_MSVC_STL_VERSION)
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan_shared.hpp>

// Clang with the MSVC ABI cannot consume Vulkan-Hpp's named module together
// with the MSVC standard-library modules. The RHI therefore includes the
// traditional Vulkan-Hpp headers in that configuration. Keep the default
// dispatcher storage in the same global-module domain as those declarations.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

// Clang emits in-class SharedHandleBase destructors as weak COFF definitions.
// When their users live in another object of the same static library, those
// definitions are not always entered in the archive index and lld-link reports
// them as unresolved. Explicit instantiation centralizes every shared handle
// used by the RHI in this object so the archive exposes a canonical definition.
#define FYUU_RHI_INSTANTIATE_SHARED_HANDLE(handle) \
	template class vk::SharedHandleBase< \
		vk::handle, \
		vk::SharedHeader< \
			vk::DestructorTypeOf<vk::handle>, \
			vk::SharedHandleTraits<vk::handle>::deleter \
		>, \
		vk::SharedHandle<vk::handle> \
	>

FYUU_RHI_INSTANTIATE_SHARED_HANDLE(BufferView);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(CommandPool);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(DebugUtilsMessengerEXT);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(DescriptorPool);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(DescriptorSetLayout);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(Device);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(Fence);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(Framebuffer);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(ImageView);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(Instance);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(Pipeline);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(PipelineCache);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(PipelineLayout);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(RenderPass);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(Sampler);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(Semaphore);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(ShaderModule);
FYUU_RHI_INSTANTIATE_SHARED_HANDLE(SurfaceKHR);

#undef FYUU_RHI_INSTANTIATE_SHARED_HANDLE
#endif // defined(__clang__) && defined(_MSVC_STL_VERSION)
