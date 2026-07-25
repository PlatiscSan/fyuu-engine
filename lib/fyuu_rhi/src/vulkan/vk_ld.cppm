module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <type_traits>
#include <stdexcept>
#include <utility>
#include <memory>
#include <array>
#include <unordered_map>
#include <fstream>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <mutex>
#include <memory_resource>
#include <filesystem>
#include <variant>
#include <optional>
#include <span>
#include <format>
#include <concepts>
#include <compare>
#include <source_location>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#include <vma/vk_mem_alloc.h>
#endif //!defined(__APPLE__)
#include <boost/scope/unique_resource.hpp>
#include <boost/scope/defer.hpp>

#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>
#include <glslang/Include/ResourceLimits.h>

#include <spirv_cross.hpp>
#include <spirv_glsl.hpp>

#include "log.hpp"
module fyuu_rhi:vulkan_logical_device;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import vulkan;
import :vulkan_traits;
import :core_types;
import :vulkan_queue_allocator;
import :log;
import :sampler_types;
import :pipeline_types;
import :slang_pipeline_interface;
import :slang;
import :cache_system;
import :native_pipeline_binding;
import plastic.lru;
import plastic.static_list;
import plastic.static_hash_table;

namespace fs = std::filesystem;

namespace {

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::pipeline;
	using namespace fyuu_rhi::vulkan;

	QueuePriority GetSchedulerQueuePriority(float priority) noexcept {
		if (priority >= 0.75f) {
			return QueuePriority::High;
		}
		if (priority >= 0.25f) {
			return QueuePriority::Medium;
		}
		return QueuePriority::Low;
	}

	struct GetTextureHandle {
		vk::Image operator()(Backend::Resource::Texture const& resource) const {
			return resource.vk_handle;
		}
		template <class T>
		vk::Image operator()(T const&) const {
			throw std::invalid_argument("Not a valid texture");
		}
	};

	struct GetBufferHandle {
		vk::Buffer operator()(Backend::Resource::Buffer const& resource) const {
			return resource.vk_handle;
		}
		template <class T>
		vk::Buffer operator()(T const&) const {
			throw std::invalid_argument("Not a valid buffer");
		}
	};

	VmaAllocationCreateFlags ExtractAllocationFlags(ResourceFlags const& flags) {
		
		VmaAllocationCreateFlags vma_flags{};
		
		bool is_conflicting = flags.TestSingleInRange(ResourceFlagBits::UndedicatedAllocation, ResourceFlagBits::DedicatedAllocation);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractAllocationFlags(): UndedicatedAllocation and DedicatedAllocation are set simultaneously");
		}
		if (flags.Test(ResourceFlagBits::UndedicatedAllocation)) {
			vma_flags |= VmaAllocationCreateFlagBits::VMA_ALLOCATION_CREATE_NEVER_ALLOCATE_BIT;
		}
		else if (flags.Test(ResourceFlagBits::DedicatedAllocation)) {
			vma_flags |= VmaAllocationCreateFlagBits::VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		}
		else {

		}

		if (flags.Test(ResourceFlagBits::AllocationWithinBudget)) {
			vma_flags |= VmaAllocationCreateFlagBits::VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT;
		}
		if (flags.Test(ResourceFlagBits::AllocationAtUpperAddress)) {
			vma_flags |= VmaAllocationCreateFlagBits::VMA_ALLOCATION_CREATE_UPPER_ADDRESS_BIT;
		}
		if (flags.Test(ResourceFlagBits::AllocationAliasAllowed)) {
			vma_flags |= VmaAllocationCreateFlagBits::VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
		}

		is_conflicting = flags.TestSingleInRange(ResourceFlagBits::MinOffsetAllocation, ResourceFlagBits::FirstFitAllocation);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractAllocationFlags(): MinOffsetAllocation BestFitAllocation or FirstFitAllocation are set simultaneously");
		}
		else if (flags.Test(ResourceFlagBits::MinOffsetAllocation)) {
			vma_flags |= VmaAllocationCreateFlagBits::VMA_ALLOCATION_CREATE_STRATEGY_MIN_OFFSET_BIT;
		}
		else if (flags.Test(ResourceFlagBits::BestFitAllocation)) {
			vma_flags |= VmaAllocationCreateFlagBits::VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT;
		}
		else if (flags.Test(ResourceFlagBits::FirstFitAllocation)) {
			vma_flags |= VmaAllocationCreateFlagBits::VMA_ALLOCATION_CREATE_STRATEGY_FIRST_FIT_BIT;
		}
		else {

		}

		is_conflicting = flags.TestSingleInRange(ResourceFlagBits::DeviceLocal, ResourceFlagBits::DeviceReadback);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractAllocationFlags(): DeviceLocal HostVisible or DeviceReadback are set simultaneously");
		}
		if (flags.Test(ResourceFlagBits::HostVisible)) {
			vma_flags |= VmaAllocationCreateFlagBits::VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
		}
		else if (flags.Test(ResourceFlagBits::DeviceReadback)) {
			vma_flags |= VmaAllocationCreateFlagBits::VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
		}
		else {

		}

		return vma_flags;

	}

	VmaMemoryUsage ExtractMemoryUsage(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestSingleInRange(ResourceFlagBits::DeviceLocal, ResourceFlagBits::DeviceReadback);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractMemoryUsage(): DeviceLocal HostVisible or DeviceReadback are set simultaneously");
		}
		if (flags.Test(ResourceFlagBits::DeviceLocal)) {
			return VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		}
		else if (flags.Test(ResourceFlagBits::HostVisible)) {
			return VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO;
		}
		else if (flags.Test(ResourceFlagBits::DeviceReadback)) {
			return VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO;
		}
		else {
			return VmaMemoryUsage::VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
		}
	}

	vk::BufferUsageFlags ExtractBufferUsage(ResourceFlags const& flags) noexcept {
		vk::BufferUsageFlags vk_flags;
		if (flags.Test(ResourceFlagBits::CopySRC)) {
			vk_flags |= vk::BufferUsageFlagBits::eTransferSrc;
		}
		if (flags.Test(ResourceFlagBits::CopyDST)) {
			vk_flags |= vk::BufferUsageFlagBits::eTransferDst;
		}
		if (flags.Test(ResourceFlagBits::UniformTexelBuffer)) {
			vk_flags |= vk::BufferUsageFlagBits::eUniformTexelBuffer;
		}
		if (flags.Test(ResourceFlagBits::StorageTexelBuffer)) {
			vk_flags |= vk::BufferUsageFlagBits::eStorageTexelBuffer;
		}
		if (flags.Test(ResourceFlagBits::UniformBuffer)) {
			vk_flags |= vk::BufferUsageFlagBits::eUniformBuffer;
		}
		if (flags.Test(ResourceFlagBits::StorageBuffer)) {
			vk_flags |= vk::BufferUsageFlagBits::eStorageBuffer;
		}
		if (flags.Test(ResourceFlagBits::IndexBuffer)) {
			vk_flags |= vk::BufferUsageFlagBits::eIndexBuffer;
		}
		if (flags.Test(ResourceFlagBits::VertexBuffer)) {
			vk_flags |= vk::BufferUsageFlagBits::eVertexBuffer;
		}
		if (flags.Test(ResourceFlagBits::IndirectBuffer)) {
			vk_flags |= vk::BufferUsageFlagBits::eIndirectBuffer;
		}
		return vk_flags;
	}

	vk::ImageType ExtractTextureDimension(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestSingleInRange(ResourceFlagBits::Texture1D, ResourceFlagBits::Texture3D);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractTextureDimension(): Texture1D Texture2D or Texture3D are set simultaneously");
		}
		if (flags.Test(ResourceFlagBits::Texture1D)) {
			return vk::ImageType::e1D;
		}
		else if (flags.Test(ResourceFlagBits::Texture2D)) {
			return vk::ImageType::e2D;
		}
		else if (flags.Test(ResourceFlagBits::Texture3D)) {
			return vk::ImageType::e3D;
		}
		else {
			return vk::ImageType::e2D;
		}
	}

	vk::Format ExtractFormat(ResourceFlags const& flags) {

		bool is_conflicting = flags.TestSingleInRange(ResourceFlagBits::R8Unorm, ResourceFlagBits::Bc7UnormSrgb);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractFormat(): Only one format can be set");
		}

		if (flags.Test(ResourceFlagBits::R8Unorm)) return vk::Format::eR8Unorm;
		else if (flags.Test(ResourceFlagBits::R8Snorm)) return vk::Format::eR8Snorm;
		else if (flags.Test(ResourceFlagBits::R8Uint)) return vk::Format::eR8Uint;
		else if (flags.Test(ResourceFlagBits::R8Sint)) return vk::Format::eR8Sint;

		else if (flags.Test(ResourceFlagBits::R8G8Unorm)) return vk::Format::eR8G8Unorm;
		else if (flags.Test(ResourceFlagBits::R8G8Snorm)) return vk::Format::eR8G8Snorm;
		else if (flags.Test(ResourceFlagBits::R8G8Uint)) return vk::Format::eR8G8Uint;
		else if (flags.Test(ResourceFlagBits::R8G8Sint)) return vk::Format::eR8G8Sint;

		else if (flags.Test(ResourceFlagBits::R8G8B8A8Unorm)) return vk::Format::eR8G8B8A8Unorm;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Snorm)) return vk::Format::eR8G8B8A8Snorm;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Uint)) return vk::Format::eR8G8B8A8Uint;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Sint)) return vk::Format::eR8G8B8A8Sint;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Srgb)) return vk::Format::eR8G8B8A8Srgb;
		else if (flags.Test(ResourceFlagBits::B8G8R8A8Srgb)) return vk::Format::eB8G8R8A8Srgb;

		else if (flags.Test(ResourceFlagBits::R16Unorm)) return vk::Format::eR16Unorm;
		else if (flags.Test(ResourceFlagBits::R16Snorm)) return vk::Format::eR16Snorm;
		else if (flags.Test(ResourceFlagBits::R16Uint)) return vk::Format::eR16Uint;
		else if (flags.Test(ResourceFlagBits::R16Sint)) return vk::Format::eR16Sint;
		else if (flags.Test(ResourceFlagBits::R16Float)) return vk::Format::eR16Sfloat;

		else if (flags.Test(ResourceFlagBits::R16G16Unorm)) return vk::Format::eR16G16Unorm;
		else if (flags.Test(ResourceFlagBits::R16G16Snorm)) return vk::Format::eR16G16Snorm;
		else if (flags.Test(ResourceFlagBits::R16G16Uint)) return vk::Format::eR16G16Uint;
		else if (flags.Test(ResourceFlagBits::R16G16Sint)) return vk::Format::eR16G16Sint;
		else if (flags.Test(ResourceFlagBits::R16G16Float)) return vk::Format::eR16G16Sfloat;

		else if (flags.Test(ResourceFlagBits::R16G16B16A16Unorm)) return vk::Format::eR16G16B16A16Unorm;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Snorm)) return vk::Format::eR16G16B16A16Snorm;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Uint)) return vk::Format::eR16G16B16A16Uint;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Sint)) return vk::Format::eR16G16B16A16Sint;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Float)) return vk::Format::eR16G16B16A16Sfloat;

		else if (flags.Test(ResourceFlagBits::R32Uint)) return vk::Format::eR32Uint;
		else if (flags.Test(ResourceFlagBits::R32Sint)) return vk::Format::eR32Sint;
		else if (flags.Test(ResourceFlagBits::R32Float)) return vk::Format::eR32Sfloat;

		else if (flags.Test(ResourceFlagBits::R32G32Uint)) return vk::Format::eR32G32Uint;
		else if (flags.Test(ResourceFlagBits::R32G32Sint)) return vk::Format::eR32G32Sint;
		else if (flags.Test(ResourceFlagBits::R32G32Float)) return vk::Format::eR32G32Sfloat;

		else if (flags.Test(ResourceFlagBits::R32G32B32A32Uint)) return vk::Format::eR32G32B32A32Uint;
		else if (flags.Test(ResourceFlagBits::R32G32B32A32Sint)) return vk::Format::eR32G32B32A32Sint;
		else if (flags.Test(ResourceFlagBits::R32G32B32A32Float)) return vk::Format::eR32G32B32A32Sfloat;

		// Packed formats
		else if (flags.Test(ResourceFlagBits::R10G10B10A2Unorm)) return vk::Format::eA2R10G10B10UnormPack32;
		else if (flags.Test(ResourceFlagBits::R10G10B10A2Uint)) return vk::Format::eA2R10G10B10UintPack32;
		else if (flags.Test(ResourceFlagBits::R11G11B10Float)) return vk::Format::eB10G11R11UfloatPack32;
		else if (flags.Test(ResourceFlagBits::R9G9B9E5SharedExp)) return vk::Format::eE5B9G9R9UfloatPack32;

		// Depth/stencil
		else if (flags.Test(ResourceFlagBits::D16Unorm)) return vk::Format::eD16Unorm;
		else if (flags.Test(ResourceFlagBits::D24UnormS8Uint)) return vk::Format::eD24UnormS8Uint;
		else if (flags.Test(ResourceFlagBits::D32Float)) return vk::Format::eD32Sfloat;
		else if (flags.Test(ResourceFlagBits::D32FloatS8X24Uint)) return vk::Format::eD32SfloatS8Uint;

		// BC compressed
		else if (flags.Test(ResourceFlagBits::Bc1Unorm)) return vk::Format::eBc1RgbaUnormBlock;
		else if (flags.Test(ResourceFlagBits::Bc1UnormSrgb)) return vk::Format::eBc1RgbaSrgbBlock;
		else if (flags.Test(ResourceFlagBits::Bc2Unorm)) return vk::Format::eBc2UnormBlock;
		else if (flags.Test(ResourceFlagBits::Bc2UnormSrgb)) return vk::Format::eBc2SrgbBlock;
		else if (flags.Test(ResourceFlagBits::Bc3Unorm)) return vk::Format::eBc3UnormBlock;
		else if (flags.Test(ResourceFlagBits::Bc3UnormSrgb)) return vk::Format::eBc3SrgbBlock;
		else if (flags.Test(ResourceFlagBits::Bc4Unorm)) return vk::Format::eBc4UnormBlock;
		else if (flags.Test(ResourceFlagBits::Bc4Snorm)) return vk::Format::eBc4SnormBlock;
		else if (flags.Test(ResourceFlagBits::Bc5Unorm)) return vk::Format::eBc5UnormBlock;
		else if (flags.Test(ResourceFlagBits::Bc5Snorm)) return vk::Format::eBc5SnormBlock;
		else if (flags.Test(ResourceFlagBits::Bc6HUfloat)) return vk::Format::eBc6HUfloatBlock;
		else if (flags.Test(ResourceFlagBits::Bc6HSfloat)) return vk::Format::eBc6HSfloatBlock;
		else if (flags.Test(ResourceFlagBits::Bc7Unorm)) return vk::Format::eBc7UnormBlock;
		else if (flags.Test(ResourceFlagBits::Bc7UnormSrgb)) return vk::Format::eBc7SrgbBlock;

		else return vk::Format::eUndefined;

	}

	vk::SampleCountFlagBits ExtractSampleCount(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestSingleInRange(ResourceFlagBits::Sample1, ResourceFlagBits::Sample64);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractSampleCount(): Only sample count can be set");
		}
		if (flags.Test(ResourceFlagBits::Sample1)) {
			return vk::SampleCountFlagBits::e1;
		}
		else if (flags.Test(ResourceFlagBits::Sample2)) {
			return vk::SampleCountFlagBits::e2;
		}
		else if (flags.Test(ResourceFlagBits::Sample4)) {
			return vk::SampleCountFlagBits::e4;
		}
		else if (flags.Test(ResourceFlagBits::Sample8)) {
			return vk::SampleCountFlagBits::e8;
		}
		else if (flags.Test(ResourceFlagBits::Sample16)) {
			return vk::SampleCountFlagBits::e16;
		}
		else if (flags.Test(ResourceFlagBits::Sample32)) {
			return vk::SampleCountFlagBits::e32;
		}
		else if (flags.Test(ResourceFlagBits::Sample64)) {
			return vk::SampleCountFlagBits::e64;
		}
		else {
			return vk::SampleCountFlagBits::e1;
		}
		
	}

	vk::ImageTiling ExtractTiling(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestSingleInRange(ResourceFlagBits::DeviceLocal, ResourceFlagBits::DeviceReadback);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractTiling(): DeviceLocal HostVisible or DeviceReadback are set simultaneously");
		}
		if (flags.Test(ResourceFlagBits::DeviceLocal)) {
			return vk::ImageTiling::eOptimal;
		}
		else if (flags.Test(ResourceFlagBits::HostVisible)) {
			return vk::ImageTiling::eLinear;
		}
		else if (flags.Test(ResourceFlagBits::DeviceReadback)) {
			return vk::ImageTiling::eLinear;
		}
		else {
			return vk::ImageTiling::eOptimal;
		}
		
	}

	vk::ImageUsageFlags ExtractTextureUsage(ResourceFlags const& flags) {
		vk::ImageUsageFlags usage{};

		if (flags.Test(ResourceFlagBits::CopySRC)) {
			usage |= vk::ImageUsageFlagBits::eTransferSrc;
		}
		if (flags.Test(ResourceFlagBits::CopyDST)) {
			usage |= vk::ImageUsageFlagBits::eTransferDst;
		}
		if (flags.Test(ResourceFlagBits::TextureBinding)) {
			usage |= vk::ImageUsageFlagBits::eSampled;
		}
		if (flags.Test(ResourceFlagBits::StorageBinding)) {
			usage |= vk::ImageUsageFlagBits::eStorage;
		}
		if (flags.Test(ResourceFlagBits::RenderAttachment)) {
			switch (ExtractFormat(flags)) {
			case vk::Format::eD16Unorm:
			case vk::Format::eD32Sfloat:
			case vk::Format::eS8Uint:
			case vk::Format::eD24UnormS8Uint:
			case vk::Format::eD32SfloatS8Uint:
				usage |= vk::ImageUsageFlagBits::eDepthStencilAttachment;
				break;
			default:
				usage |= vk::ImageUsageFlagBits::eColorAttachment;
				break;
			}
		}
		if (flags.Test(ResourceFlagBits::TransientAttachment)) {
			usage |= vk::ImageUsageFlagBits::eTransientAttachment;
		}
		if (flags.Test(ResourceFlagBits::StorageAttachment)) {
			// StorageAttachment is interpreted as input attachment
			usage |= vk::ImageUsageFlagBits::eInputAttachment;
		}

		return usage;
	}

	vk::ImageViewType ExtractTextureViewType(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestSingleInRange(ResourceFlagBits::TextureView1D, ResourceFlagBits::TextureView3D);
		if (is_conflicting) {
			throw std::invalid_argument("ExtractTextureViewType(): Only one texture view type can be set");
		}
		if (flags.Test(ResourceFlagBits::TextureView1D)) {
			return vk::ImageViewType::e1D;
		}
		else if (flags.Test(ResourceFlagBits::TextureView2D)) {
			return vk::ImageViewType::e2D;
		}
		else if (flags.Test(ResourceFlagBits::TextureView3D)) {
			return vk::ImageViewType::e3D;
		}
		else if (flags.Test(ResourceFlagBits::TextureViewCube)) {
			return vk::ImageViewType::eCube;
		}
		else if (flags.Test(ResourceFlagBits::TextureView2DArray)) {
			return vk::ImageViewType::e2DArray;
		}
		else if (flags.Test(ResourceFlagBits::TextureViewCubeArray)) {
			return vk::ImageViewType::eCubeArray;
		}
		else {
			return vk::ImageViewType::e2D;
		}
	}

	std::uint32_t GetMultiPlaneCount(Backend::LogicalDevice const& ld, vk::Format format) {

		struct CacheKey {
			vk::Device device;
			vk::Format format;
			std::strong_ordering operator<=>(CacheKey const& other) const noexcept = default;
		};

		struct CacheKeyHash {
			std::size_t operator()(CacheKey const& key) const noexcept {
				return std::hash<vk::Device>{}(key.device) ^ (std::hash<vk::Format>{}(key.format) << 1);
			}
		};

		using List = plastic::ds::StaticList<std::pair<CacheKey const, std::uint32_t>, 64u>;

		static plastic::ds::LRUCache<
			plastic::ds::StaticHashTable<CacheKey, typename List::iterator, 64u, CacheKeyHash>,
			List,
			64u
		> cache;

		static std::mutex mutex;

		CacheKey key{ *ld.impl, format };

		if (std::unique_lock<std::mutex> lock(mutex); cache.Contains(key)) {
			return cache.Get(key);
		}

		try {
			vk::FormatProperties2 fmt_props = ld.phys_dev.impl->getFormatProperties2(format, *ld.dispatcher);
			bool disjoint_supported = (fmt_props.formatProperties.optimalTilingFeatures &
				vk::FormatFeatureFlagBits::eDisjoint) != vk::FormatFeatureFlags{};
			if (!disjoint_supported) {
				{
					std::unique_lock<std::mutex> lock(mutex);
					cache.Put(key, 0u);
				}
				return 0u;
			}
		}
		catch (std::exception const& ex) {
			LOG_WARNING(
				std::format("GetMultiPlaneCount(): Calling getFormatProperties2() but failed, Vulkan reports {}", ex.what())
			);
			return 0u;
		}

		try {
			std::uint32_t plane_count = 0;
			std::array planes = {
				vk::ImageAspectFlagBits::ePlane0,
				vk::ImageAspectFlagBits::ePlane1,
				vk::ImageAspectFlagBits::ePlane2
			};
			vk::Result res;
			for (auto plane : planes) {
				vk::ImagePlaneMemoryRequirementsInfo plane_info(plane);
				vk::ImageFormatProperties2 img_props({}, &plane_info);
				vk::PhysicalDeviceImageFormatInfo2 fmt_info(
					format,
					vk::ImageType::e2D,
					vk::ImageTiling::eOptimal,
					vk::ImageUsageFlagBits::eSampled,
					vk::ImageCreateFlagBits::eDisjoint,
					nullptr
				);
				res = ld.phys_dev.impl->getImageFormatProperties2(&fmt_info, &img_props, *ld.dispatcher);
				plane_count += res == vk::Result::eSuccess;
			}
			{
				std::unique_lock<std::mutex> lock(mutex);
				cache.Put(key, plane_count);
			}
			return plane_count;
		}
		catch (std::exception const& ex) {
			LOG_WARNING(std::format("GetMultiPlaneCount(): Calling getImageFormatProperties2() but failed, Vulkan reports {}", ex.what()));
			return 0u;
		}

	}

	vk::ImageAspectFlags ExtractTextureAspect(std::size_t base_mip_lvl, std::size_t mip_lvl_cnt, std::size_t base_arr_layer, std::size_t arr_layer_cnt, ResourceFlags const& flags, Backend::LogicalDevice const& ld) {

		vk::Format format = ExtractFormat(flags);

		bool plane0 = flags.Test(ResourceFlagBits::TextureViewAspectPlane0Only);
		bool plane1 = flags.Test(ResourceFlagBits::TextureViewAspectPlane1Only);
		bool plane2 = flags.Test(ResourceFlagBits::TextureViewAspectPlane2Only);
		std::optional<std::uint32_t> requested_plane;
		if (plane0) {
			requested_plane = 0u;
		}
		else if (plane1) {
			requested_plane = 1u;
		}
		else if (plane2) {
			requested_plane = 2u;
		}
		else {

		}

		if (requested_plane) {
			std::uint32_t plane_count = GetMultiPlaneCount(ld, format);
			if (plane_count < 2) {
				throw std::invalid_argument("ExtractTextureAspect(): TextureViewAspectPlaneXOnly used on non-multiplanar format");
			}
			if (*requested_plane >= plane_count) {
				throw std::invalid_argument("ExtractTextureAspect(): Requested plane index out of range for this format");
			}
			switch (*requested_plane) {
			case 0: 
				return vk::ImageAspectFlagBits::ePlane0;
			case 1: 
				return vk::ImageAspectFlagBits::ePlane1;
			case 2: 
				return vk::ImageAspectFlagBits::ePlane2;
			default: 
				std::unreachable();
			}
		}

		bool is_depth = format == vk::Format::eD16Unorm ||
			format == vk::Format::eD24UnormS8Uint ||
			format == vk::Format::eD32Sfloat;

		bool is_stencil = format == vk::Format::eS8Uint ||
			format == vk::Format::eD24UnormS8Uint ||
			format == vk::Format::eD32SfloatS8Uint;

		if (!is_depth && !is_stencil) {
			return vk::ImageAspectFlagBits::eColor;
		}

		if (flags.Test(ResourceFlagBits::TextureViewAspectDepthOnly)) {
			return vk::ImageAspectFlagBits::eDepth;
		}
		if (flags.Test(ResourceFlagBits::TextureViewAspectStencilOnly)) {
			return vk::ImageAspectFlagBits::eStencil;
		}

		vk::ImageAspectFlags aspect;
		if (is_depth) {
			aspect |= vk::ImageAspectFlagBits::eDepth;
		}
		if (is_stencil) {
			aspect |= vk::ImageAspectFlagBits::eStencil;
		}

		return aspect;

	}

	vk::SamplerMipmapMode MapMipmapMode(MipmapFilterMode mode) noexcept {
		switch (mode) {
		case MipmapFilterMode::Nearest: return vk::SamplerMipmapMode::eNearest;
		case MipmapFilterMode::Linear:	return vk::SamplerMipmapMode::eLinear;
		default:						return vk::SamplerMipmapMode::eLinear;
		}
	}

	vk::Filter MapFilterMode(FilterMode mode) noexcept {
		switch (mode) {
		case FilterMode::Nearest:	return vk::Filter::eNearest;
		case FilterMode::Linear:	return vk::Filter::eLinear;
		default:					return vk::Filter::eLinear;
		}
	}

	vk::SamplerAddressMode MapAddressMode(AddressMode mode) noexcept {
		switch (mode) {
		case AddressMode::Repeat:			return vk::SamplerAddressMode::eRepeat;
		case AddressMode::MirroredRepeat:	return vk::SamplerAddressMode::eMirroredRepeat;
		case AddressMode::ClampToEdge:		return vk::SamplerAddressMode::eClampToEdge;
		default:							return vk::SamplerAddressMode::eClampToEdge;
		}
	}

	vk::CompareOp MapCompareOp(CompareFunction op) noexcept {
		switch (op) {
		case CompareFunction::Never:		return vk::CompareOp::eNever;
		case CompareFunction::Less:			return vk::CompareOp::eLess;
		case CompareFunction::Equal:		return vk::CompareOp::eEqual;
		case CompareFunction::LessEqual:	return vk::CompareOp::eLessOrEqual;
		case CompareFunction::Greater:		return vk::CompareOp::eGreater;
		case CompareFunction::NotEqual:		return vk::CompareOp::eNotEqual;
		case CompareFunction::GreaterEqual: return vk::CompareOp::eGreaterOrEqual;
		case CompareFunction::Always:		return vk::CompareOp::eAlways;
		default:							return vk::CompareOp::eNever;
		}
	}

	inline int Uint32VersionToShorthand(uint32_t ver) {
		uint32_t major = vk::apiVersionMajor(ver);
		uint32_t minor = vk::apiVersionMinor(ver);
		return major * 100 + minor;
	}

	std::string_view SelectSPIRVProfile(std::uint32_t vulkan_api_version) {
		if (vulkan_api_version >= vk::ApiVersion13) {
			return "spirv_1_6";
		}
		if (vulkan_api_version >= vk::ApiVersion12) {
			return "spirv_1_5";
		}
		if (vulkan_api_version >= vk::ApiVersion11) {
			return "spirv_1_3";
		}
		return "spirv_1_0";
	}

	vk::ShaderStageFlags MapPipelineStageFlags(std::uint32_t visibility) {
		vk::ShaderStageFlags result;
		if (visibility & (1u << static_cast<std::uint32_t>(PipelineStage::Vertex))) result |= vk::ShaderStageFlagBits::eVertex;
		if (visibility & (1u << static_cast<std::uint32_t>(PipelineStage::Fragment))) result |= vk::ShaderStageFlagBits::eFragment;
		if (visibility & (1u << static_cast<std::uint32_t>(PipelineStage::TessellationControl))) result |= vk::ShaderStageFlagBits::eTessellationControl;
		if (visibility & (1u << static_cast<std::uint32_t>(PipelineStage::TessellationEvaluation))) result |= vk::ShaderStageFlagBits::eTessellationEvaluation;
		if (visibility & (1u << static_cast<std::uint32_t>(PipelineStage::Geometry))) result |= vk::ShaderStageFlagBits::eGeometry;
		if (visibility & (1u << static_cast<std::uint32_t>(PipelineStage::Task))) result |= vk::ShaderStageFlagBits::eTaskEXT;
		if (visibility & (1u << static_cast<std::uint32_t>(PipelineStage::Mesh))) result |= vk::ShaderStageFlagBits::eMeshEXT;
		if (visibility & (1u << static_cast<std::uint32_t>(PipelineStage::Compute))) result |= vk::ShaderStageFlagBits::eCompute;
		return result;
	}

	vk::DescriptorType MapPipelineDescriptorType(ResourceFlags const& flags) {
		if (flags.Test(ResourceFlagBits::SamplerBinding)) {
			return flags.Test(ResourceFlagBits::TextureBinding)
				? vk::DescriptorType::eCombinedImageSampler
				: vk::DescriptorType::eSampler;
		}
		if (flags.Test(ResourceFlagBits::UniformBuffer)) return vk::DescriptorType::eUniformBuffer;
		if (flags.Test(ResourceFlagBits::StorageBuffer)) return vk::DescriptorType::eStorageBuffer;
		if (flags.Test(ResourceFlagBits::StorageBinding)) return vk::DescriptorType::eStorageImage;
		if (flags.Test(ResourceFlagBits::TextureBinding)) return vk::DescriptorType::eSampledImage;
		throw std::invalid_argument("Unsupported Vulkan pipeline binding flags");
	}

	vk::ShaderStageFlagBits MapPipelineStage(PipelineStage stage) {
		switch (stage) {
		case PipelineStage::Vertex: return vk::ShaderStageFlagBits::eVertex;
		case PipelineStage::Fragment: return vk::ShaderStageFlagBits::eFragment;
		case PipelineStage::TessellationControl: return vk::ShaderStageFlagBits::eTessellationControl;
		case PipelineStage::TessellationEvaluation: return vk::ShaderStageFlagBits::eTessellationEvaluation;
		case PipelineStage::Geometry: return vk::ShaderStageFlagBits::eGeometry;
		case PipelineStage::Task: return vk::ShaderStageFlagBits::eTaskEXT;
		case PipelineStage::Mesh: return vk::ShaderStageFlagBits::eMeshEXT;
		default: throw std::invalid_argument("Compute stage cannot be used in a Vulkan graphics pipeline");
		}
	}

	Backend::Pipeline CreateVulkanPipelineInterface(
		Backend::LogicalDevice const& ld,
		shader::SlangProgram const& program,
		vk::PipelineBindPoint bind_point
	) {
		Backend::Pipeline pipeline;
		pipeline.bind_point = bind_point;
		pipeline.bindings = MakePipelineBindingMetadata(program.Interface());
		std::uint32_t max_space = 0u;
		for (auto const& entry : program.Interface().bindings) {
			max_space = std::max(max_space, entry.space);
		}
		std::vector<std::vector<vk::DescriptorSetLayoutBinding>> set_bindings(
			program.Interface().bindings.empty() ? 0u : max_space + 1u
		);
		for (auto const& entry : program.Interface().bindings) {
			set_bindings[entry.space].emplace_back(
				entry.slot,
				MapPipelineDescriptorType(entry.flags),
				entry.count,
				MapPipelineStageFlags(entry.visibility)
			);
		}
		std::vector<vk::DescriptorSetLayout> raw_set_layouts;
		for (auto& bindings : set_bindings) {
			std::ranges::sort(bindings, {}, &vk::DescriptorSetLayoutBinding::binding);
			vk::DescriptorSetLayoutCreateInfo info({}, bindings);
			auto raw = ld.impl->createDescriptorSetLayout(info, nullptr, *ld.dispatcher);
			pipeline.descriptor_set_layouts.push_back(
				vk::SharedDescriptorSetLayout(raw, ld.impl, { nullptr, *ld.dispatcher })
			);
			raw_set_layouts.push_back(raw);
		}
		std::vector<vk::PushConstantRange> push_constants;
		for (auto const& range : program.Interface().push_constants) {
			push_constants.emplace_back(MapPipelineStageFlags(range.visibility), range.offset, range.size);
		}
		vk::PipelineLayoutCreateInfo layout_info({}, raw_set_layouts, push_constants);
		auto raw_layout = ld.impl->createPipelineLayout(layout_info, nullptr, *ld.dispatcher);
		pipeline.layout = vk::SharedPipelineLayout(raw_layout, ld.impl, { nullptr, *ld.dispatcher });
		return pipeline;
	}

	vk::CompareOp MapPipelineCompareOperation(CompareOperation operation) {
		return static_cast<vk::CompareOp>(static_cast<std::uint32_t>(operation));
	}

	vk::StencilOp MapPipelineStencilOperation(StencilOperation operation) {
		constexpr vk::StencilOp values[] = {
			vk::StencilOp::eKeep, vk::StencilOp::eZero, vk::StencilOp::eReplace, vk::StencilOp::eInvert,
			vk::StencilOp::eIncrementAndClamp, vk::StencilOp::eDecrementAndClamp,
			vk::StencilOp::eIncrementAndWrap, vk::StencilOp::eDecrementAndWrap
		};
		return values[static_cast<std::size_t>(operation)];
	}

	vk::StencilOpState MapPipelineStencilFace(StencilFaceState const& face) {
		return vk::StencilOpState(
			MapPipelineStencilOperation(face.fail_operation),
			MapPipelineStencilOperation(face.pass_operation),
			MapPipelineStencilOperation(face.depth_fail_operation),
			MapPipelineCompareOperation(face.compare)
		);
	}

	vk::BlendFactor MapPipelineBlendFactor(BlendFactor factor) {
		switch (factor) {
		case BlendFactor::Zero: return vk::BlendFactor::eZero;
		case BlendFactor::One: return vk::BlendFactor::eOne;
		case BlendFactor::SourceColor: return vk::BlendFactor::eSrcColor;
		case BlendFactor::OneMinusSourceColor: return vk::BlendFactor::eOneMinusSrcColor;
		case BlendFactor::SourceAlpha: return vk::BlendFactor::eSrcAlpha;
		case BlendFactor::OneMinusSourceAlpha: return vk::BlendFactor::eOneMinusSrcAlpha;
		case BlendFactor::DestinationColor: return vk::BlendFactor::eDstColor;
		case BlendFactor::OneMinusDestinationColor: return vk::BlendFactor::eOneMinusDstColor;
		case BlendFactor::DestinationAlpha: return vk::BlendFactor::eDstAlpha;
		case BlendFactor::OneMinusDestinationAlpha: return vk::BlendFactor::eOneMinusDstAlpha;
		case BlendFactor::SourceAlphaSaturated: return vk::BlendFactor::eSrcAlphaSaturate;
		case BlendFactor::Constant: return vk::BlendFactor::eConstantColor;
		case BlendFactor::OneMinusConstant: return vk::BlendFactor::eOneMinusConstantColor;
		default: return vk::BlendFactor::eOne;
		}
	}

	vk::BlendOp MapPipelineBlendOperation(BlendOperation operation) {
		switch (operation) {
		case BlendOperation::Add: return vk::BlendOp::eAdd;
		case BlendOperation::Subtract: return vk::BlendOp::eSubtract;
		case BlendOperation::ReverseSubtract: return vk::BlendOp::eReverseSubtract;
		case BlendOperation::Min: return vk::BlendOp::eMin;
		case BlendOperation::Max: return vk::BlendOp::eMax;
		default: return vk::BlendOp::eAdd;
		}
	}

	using VulkanSchedulerSynchronization = std::variant<
		Backend::VulkanScheduler::TimelineSynchronization,
		std::shared_ptr<Backend::VulkanScheduler::BinarySynchronizationPool>
	>;

	VulkanSchedulerSynchronization CreateSchedulerSynchronization(Backend::LogicalDevice const& ld) {
		if (ld.enabled_features.contains(
			vk::StructureType::ePhysicalDeviceTimelineSemaphoreFeatures
		)) {
			vk::SemaphoreTypeCreateInfo type_info(vk::SemaphoreType::eTimeline, 0u);
			vk::SemaphoreCreateInfo semaphore_info({}, &type_info);
			auto semaphore = ld.impl->createSemaphore(semaphore_info, nullptr, *ld.dispatcher);
			return VulkanSchedulerSynchronization(
				std::in_place_type<Backend::VulkanScheduler::TimelineSynchronization>,
				vk::SharedSemaphore(semaphore, ld.impl, { nullptr, *ld.dispatcher })
			);
		}

		return VulkanSchedulerSynchronization(
			std::in_place_type<std::shared_ptr<Backend::VulkanScheduler::BinarySynchronizationPool>>,
			std::make_shared<Backend::VulkanScheduler::BinarySynchronizationPool>(
				ld.impl,
				ld.dispatcher
			)
		);
	}

	std::shared_ptr<Backend::VulkanScheduler::QueueState> CreateSchedulerQueue(
		Backend::LogicalDevice& ld,
		CommandQueueType capability,
		QueuePriority priority
	) {
		auto allocation = ld.queue_alloc.Allocate(capability, priority);
		auto info = allocation.GetInfo();
		auto queue = ld.impl->getQueue(info.family, info.index, *ld.dispatcher);
		auto synchronization = CreateSchedulerSynchronization(ld);
		return std::make_shared<Backend::VulkanScheduler::QueueState>(
			ld.impl,
			ld.dispatcher,
			std::move(allocation),
			vk::SharedQueue(queue, ld.impl),
			std::move(synchronization),
			std::make_shared<Backend::VulkanScheduler::CommandPool>(),
			capability,
			info.family,
			info.index
		);
	}


}

namespace fyuu_rhi::vulkan {

	using namespace fyuu_rhi::pipeline;

	Backend::Resource Backend::CreateBuffer(LogicalDevice const& ld, std::size_t size_in_bytes, ResourceFlags const& flags) {
		
		VmaAllocationCreateInfo alloc_create_info{
			ExtractAllocationFlags(flags),
			ExtractMemoryUsage(flags),
			0, 0, 0, nullptr, nullptr, 0.0f
		};

		VkBufferCreateInfo buf_info(
			vk::BufferCreateInfo(
				{},
				size_in_bytes,
				ExtractBufferUsage(flags),
				vk::SharingMode::eExclusive,
				0u,
				nullptr,
				nullptr
			)
		);

		VkBuffer buf;
		VmaAllocation alloc;
		VmaAllocationInfo alloc_info;

		auto result = static_cast<vk::Result>(
			vmaCreateBuffer(
				ld.mem_alloc->impl,
				&buf_info,
				&alloc_create_info,
				&buf,
				&alloc,
				&alloc_info
			));

		if (result != vk::Result::eSuccess) {
			throw std::runtime_error(std::format("Calling vmaCreateBuffer() failed, VMA reported: {}", vk::to_string(result)));
		}

		return { Backend::Resource::Buffer(ld.mem_alloc, buf_info, buf, alloc, alloc_info) };

	}

	Backend::Resource Backend::CreateTexture(LogicalDevice const& ld, std::size_t width, std::size_t height, std::size_t depth_arr_layers, std::size_t mip_lvl_cnt, ResourceFlags const& flags) {
		
		VmaAllocationCreateInfo alloc_create_info{
			ExtractAllocationFlags(flags),
			ExtractMemoryUsage(flags),
			0, 0, 0, nullptr, nullptr, 0.0f
		};

		vk::ImageType dim = ExtractTextureDimension(flags);

		VkImageCreateInfo tex_info(
			vk::ImageCreateInfo(
				{},
				dim,
				ExtractFormat(flags),
				{ static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), dim == vk::ImageType::e3D ? static_cast<std::uint32_t>(depth_arr_layers) : 1u },
				static_cast<std::uint32_t>(mip_lvl_cnt),
				dim == vk::ImageType::e3D ? 1u : static_cast<std::uint32_t>(depth_arr_layers),
				ExtractSampleCount(flags),
				ExtractTiling(flags),
				ExtractTextureUsage(flags),
				vk::SharingMode::eExclusive,
				0u,
				nullptr,
				vk::ImageLayout::eUndefined,
				nullptr
			)
		);

		VkImage tex;
		VmaAllocation alloc;
		VmaAllocationInfo alloc_info;

		auto result = static_cast<vk::Result>(
			vmaCreateImage(
				ld.mem_alloc->impl,
				&tex_info,
				&alloc_create_info,
				&tex,
				&alloc,
				&alloc_info
			));

		if (result != vk::Result::eSuccess) {
			throw std::runtime_error(std::format("Calling vmaCreateImage() failed, VMA reported: {}", vk::to_string(result)));
		}

		return {
			Backend::Resource::Texture(
				ld.mem_alloc,
				tex_info,
				tex,
				alloc,
				alloc_info,
				vk::ImageLayout::eUndefined,
				vk::ImageLayout::eUndefined
			)
		};

	}

	Backend::View Backend::CreateTextureView(Backend::LogicalDevice const& ld, Backend::Resource const& res, std::size_t base_mip_lvl, std::size_t mip_lvl_cnt, std::size_t base_arr_layer, std::size_t arr_layer_cnt, ResourceFlags const& flags) {

		vk::Image tex = std::visit(GetTextureHandle{}, res.impl);

		vk::ImageViewCreateInfo info(
			{},
			tex,
			ExtractTextureViewType(flags),
			ExtractFormat(flags),
			{
				vk::ComponentSwizzle::eIdentity,
				vk::ComponentSwizzle::eIdentity,
				vk::ComponentSwizzle::eIdentity,
				vk::ComponentSwizzle::eIdentity
			},
			{
				ExtractTextureAspect(base_mip_lvl, mip_lvl_cnt, base_arr_layer, arr_layer_cnt, flags, ld),
				static_cast<std::uint32_t>(base_mip_lvl),
				static_cast<std::uint32_t>(mip_lvl_cnt),
				static_cast<std::uint32_t>(base_arr_layer),
				static_cast<std::uint32_t>(arr_layer_cnt)
			}
		);

		vk::ImageView raw = ld.impl->createImageView(info, nullptr, *ld.dispatcher);
		vk::SharedImageView shared_view(raw, ld.impl, { nullptr, *ld.dispatcher });
		return { Backend::View::Texture{ info, std::move(shared_view) } };

	}

	Backend::View Backend::CreateBufferView(LogicalDevice const& ld, Resource const& res, std::size_t offset, std::size_t range, ResourceFlags const& flags) {
		
		vk::Buffer buf = std::visit(GetBufferHandle{}, res.impl);
		
		vk::BufferViewCreateInfo info(
			{},
			buf,
			ExtractFormat(flags),
			offset,
			range
		);
		
		vk::BufferView raw = ld.impl->createBufferView(info, nullptr, *ld.dispatcher);
		vk::SharedBufferView shared_view(raw, ld.impl, { nullptr, *ld.dispatcher });
		return { Backend::View::Buffer{ info, std::move(shared_view) } };

	}

	vk::SharedSampler Backend::CreateSampler(Backend::LogicalDevice const& ld, SamplerDescriptor const& descriptor) {

		vk::SamplerCreateInfo info(
			{},
			MapFilterMode(descriptor.min_filter),
			MapFilterMode(descriptor.mag_filter),
			MapMipmapMode(descriptor.mipmap_filter),
			MapAddressMode(descriptor.address_mode_u),
			MapAddressMode(descriptor.address_mode_v),
			MapAddressMode(descriptor.address_mode_w),
			0.0f,
			(descriptor.max_anisotropy > 1u) ? vk::True : vk::False,
			descriptor.max_anisotropy,
			(descriptor.compare_function != CompareFunction::Unknown) ? vk::True : vk::False,
			MapCompareOp(descriptor.compare_function),
			descriptor.min_lod,
			descriptor.max_lod,
			vk::BorderColor::eFloatTransparentBlack,
			vk::False
		);

		vk::Sampler raw = ld.impl->createSampler(info, nullptr, *ld.dispatcher);
		vk::SharedSampler shared_sampler(raw, ld.impl, { nullptr, *ld.dispatcher });
		return { std::move(shared_sampler) };

	}

	Backend::Pipeline Backend::CreateGraphicsPipeline(
		Backend::LogicalDevice const& ld,
		GraphicsPipelineDescriptor const& descriptor
	) {
		auto properties = ld.phys_dev.impl->getProperties(*ld.dispatcher);
		auto spirv_profile = SelectSPIRVProfile(properties.apiVersion);
		slang::TargetDesc target{ .format = SLANG_SPIRV };
		target.profile = shader::SlangGlobalSession()->findProfile(spirv_profile.data());
		auto cache_tag = std::format(
			"vulkan-{:04x}-{:04x}-{:08x}-api-{:08x}-{}",
			properties.vendorID,
			properties.deviceID,
			properties.driverVersion,
			properties.apiVersion,
			spirv_profile
		);
		shader::SlangProgram program(target, descriptor.program, cache_tag);
		auto pipeline = CreateVulkanPipelineInterface(ld, program, vk::PipelineBindPoint::eGraphics);
		auto raw_layout = *pipeline.layout;

		std::vector<vk::SharedShaderModule> modules;
		std::vector<vk::PipelineShaderStageCreateInfo> stages;
		for (auto const& entry : program.EntryPoints()) {
			if (entry.code.size() % sizeof(std::uint32_t) != 0) {
				throw std::runtime_error("Slang returned misaligned SPIR-V bytecode");
			}
			vk::ShaderModuleCreateInfo module_info(
				{},
				entry.code.size(),
				reinterpret_cast<std::uint32_t const*>(entry.code.data())
			);
			auto raw = ld.impl->createShaderModule(module_info, nullptr, *ld.dispatcher);
			modules.push_back(vk::SharedShaderModule(raw, ld.impl, { nullptr, *ld.dispatcher }));
			stages.emplace_back(vk::PipelineShaderStageCreateFlags{}, MapPipelineStage(entry.stage), raw, entry.name.c_str());
		}

		std::vector<vk::VertexInputBindingDescription> vertex_bindings;
		for (auto const& binding : descriptor.vertex.buffers) {
			vertex_bindings.emplace_back(
				binding.slot,
				binding.stride,
				binding.input_rate == VertexInputRate::Vertex ? vk::VertexInputRate::eVertex : vk::VertexInputRate::eInstance
			);
		}
		std::vector<vk::VertexInputAttributeDescription> vertex_attributes;
		for (auto const& attribute : descriptor.vertex.attributes) {
			vertex_attributes.emplace_back(
				attribute.location,
				attribute.slot,
				ExtractFormat(ResourceFlags(attribute.format)),
				attribute.offset
			);
		}
		vk::PipelineVertexInputStateCreateInfo vertex_input({}, vertex_bindings, vertex_attributes);
		auto topology =
			descriptor.primitive.topology == PrimitiveTopology::PointList ? vk::PrimitiveTopology::ePointList :
			descriptor.primitive.topology == PrimitiveTopology::LineList ? vk::PrimitiveTopology::eLineList :
			descriptor.primitive.topology == PrimitiveTopology::LineStrip ? vk::PrimitiveTopology::eLineStrip :
			descriptor.primitive.topology == PrimitiveTopology::TriangleStrip ? vk::PrimitiveTopology::eTriangleStrip :
			vk::PrimitiveTopology::eTriangleList;
		vk::PipelineInputAssemblyStateCreateInfo input_assembly({}, topology, descriptor.primitive.strip_index_format.has_value());
		vk::PipelineViewportStateCreateInfo viewport({}, 1, nullptr, 1, nullptr);
		vk::PipelineRasterizationStateCreateInfo raster(
			{}, false, false, vk::PolygonMode::eFill,
			descriptor.rasterization.cull_mode == CullMode::Front ? vk::CullModeFlagBits::eFront :
			descriptor.rasterization.cull_mode == CullMode::Back ? vk::CullModeFlagBits::eBack : vk::CullModeFlagBits::eNone,
			descriptor.rasterization.front_face == FrontFace::CounterClockwise ? vk::FrontFace::eCounterClockwise : vk::FrontFace::eClockwise,
			descriptor.rasterization.depth_bias.constant != 0 || descriptor.rasterization.depth_bias.slope_scale != 0,
			static_cast<float>(descriptor.rasterization.depth_bias.constant),
			descriptor.rasterization.depth_bias.clamp,
			descriptor.rasterization.depth_bias.slope_scale,
			1.0f
		);
		vk::PipelineMultisampleStateCreateInfo multisample(
			{}, ExtractSampleCount(ResourceFlags(descriptor.multisample.sample_count)),
			false, 0.0f, &descriptor.multisample.mask,
			descriptor.multisample.alpha_to_coverage_enabled, false
		);
		vk::PipelineDepthStencilStateCreateInfo depth_stencil;
		vk::Format depth_format = vk::Format::eUndefined;
		if (descriptor.depth_stencil) {
			auto const& state = *descriptor.depth_stencil;
			depth_format = ExtractFormat(ResourceFlags(state.format));
			depth_stencil = vk::PipelineDepthStencilStateCreateInfo(
				{}, state.depth_test_enabled, state.depth_write_enabled, MapPipelineCompareOperation(state.depth_compare),
				false, state.stencil_enabled, MapPipelineStencilFace(state.stencil_front), MapPipelineStencilFace(state.stencil_back)
			);
		}
		std::vector<vk::PipelineColorBlendAttachmentState> blend_attachments;
		std::vector<vk::Format> color_formats;
		for (auto const& target_state : descriptor.color_targets) {
			color_formats.push_back(ExtractFormat(ResourceFlags(target_state.format)));
			vk::PipelineColorBlendAttachmentState attachment;
			attachment.colorWriteMask = static_cast<vk::ColorComponentFlags>(static_cast<std::uint8_t>(target_state.write_mask));
			if (target_state.blend) {
				auto const& state = *target_state.blend;
				attachment.blendEnable = true;
				attachment.srcColorBlendFactor = MapPipelineBlendFactor(state.color.source_factor);
				attachment.dstColorBlendFactor = MapPipelineBlendFactor(state.color.destination_factor);
				attachment.colorBlendOp = MapPipelineBlendOperation(state.color.operation);
				attachment.srcAlphaBlendFactor = MapPipelineBlendFactor(state.alpha.source_factor);
				attachment.dstAlphaBlendFactor = MapPipelineBlendFactor(state.alpha.destination_factor);
				attachment.alphaBlendOp = MapPipelineBlendOperation(state.alpha.operation);
			}
			blend_attachments.push_back(attachment);
		}
		vk::PipelineColorBlendStateCreateInfo blend({}, false, vk::LogicOp::eCopy, blend_attachments);
		std::array dynamic_states{ vk::DynamicState::eViewport, vk::DynamicState::eScissor };
		vk::PipelineDynamicStateCreateInfo dynamic({}, dynamic_states);
		auto stencil_format =
			depth_format == vk::Format::eD24UnormS8Uint || depth_format == vk::Format::eD32SfloatS8Uint
			? depth_format
			: vk::Format::eUndefined;
		vk::PipelineRenderingCreateInfo rendering({}, color_formats, depth_format, stencil_format);
		vk::RenderPass compatible_render_pass;
		auto dynamic_rendering_enabled = ld.enabled_features.contains(
			vk::StructureType::ePhysicalDeviceDynamicRenderingFeatures
		);
		if (!dynamic_rendering_enabled) {
			auto samples = ExtractSampleCount(ResourceFlags(descriptor.multisample.sample_count));
			std::vector<vk::AttachmentDescription> attachments;
			std::vector<vk::AttachmentReference> color_references;
			attachments.reserve(color_formats.size() + (depth_format != vk::Format::eUndefined ? 1u : 0u));
			color_references.reserve(color_formats.size());
			for (auto format : color_formats) {
				auto index = static_cast<std::uint32_t>(attachments.size());
				attachments.emplace_back(
					vk::AttachmentDescriptionFlags{},
					format,
					samples,
					vk::AttachmentLoadOp::eDontCare,
					vk::AttachmentStoreOp::eStore,
					vk::AttachmentLoadOp::eDontCare,
					vk::AttachmentStoreOp::eDontCare,
					vk::ImageLayout::eUndefined,
					vk::ImageLayout::eColorAttachmentOptimal
				);
				color_references.emplace_back(index, vk::ImageLayout::eColorAttachmentOptimal);
			}
			std::optional<vk::AttachmentReference> depth_reference;
			if (depth_format != vk::Format::eUndefined) {
				auto index = static_cast<std::uint32_t>(attachments.size());
				attachments.emplace_back(
					vk::AttachmentDescriptionFlags{},
					depth_format,
					samples,
					vk::AttachmentLoadOp::eDontCare,
					vk::AttachmentStoreOp::eStore,
					vk::AttachmentLoadOp::eDontCare,
					vk::AttachmentStoreOp::eStore,
					vk::ImageLayout::eUndefined,
					vk::ImageLayout::eDepthStencilAttachmentOptimal
				);
				depth_reference.emplace(index, vk::ImageLayout::eDepthStencilAttachmentOptimal);
			}
			vk::SubpassDescription subpass;
			subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
			subpass.colorAttachmentCount = static_cast<std::uint32_t>(color_references.size());
			subpass.pColorAttachments = color_references.data();
			subpass.pDepthStencilAttachment = depth_reference ? &*depth_reference : nullptr;
			std::array subpasses{ subpass };
			vk::RenderPassCreateInfo render_pass_info(
				vk::RenderPassCreateFlags{},
				attachments,
				subpasses
			);
			compatible_render_pass = ld.impl->createRenderPass(render_pass_info, nullptr, *ld.dispatcher);
			pipeline.compatible_render_pass = vk::SharedRenderPass(
				compatible_render_pass,
				ld.impl,
				{ nullptr, *ld.dispatcher }
			);
		}
		vk::GraphicsPipelineCreateInfo info(
			{}, stages, &vertex_input, &input_assembly, nullptr, &viewport, &raster,
			&multisample, descriptor.depth_stencil ? &depth_stencil : nullptr, &blend, &dynamic,
			raw_layout,
			compatible_render_pass
		);
		info.pNext = dynamic_rendering_enabled ? &rendering : nullptr;

		auto cache_path = cache::GetCacheFilePath(std::format(
			"vulkan-pipeline-{:04x}-{:04x}-{:08x}.bin",
			properties.vendorID, properties.deviceID, properties.driverVersion
		));
		auto cache_data = cache::ReadFile(cache_path);
		vk::PipelineCacheCreateInfo cache_info({}, cache_data.size(), cache_data.data());
		auto raw_cache = ld.impl->createPipelineCache(cache_info, nullptr, *ld.dispatcher);
		vk::SharedPipelineCache pipeline_cache(raw_cache, ld.impl, { nullptr, *ld.dispatcher });
		auto creation = ld.impl->createGraphicsPipeline(raw_cache, info, nullptr, *ld.dispatcher);
		pipeline.impl = vk::SharedPipeline(creation.value, ld.impl, { nullptr, *ld.dispatcher });
		auto updated_cache = ld.impl->getPipelineCacheData(raw_cache, *ld.dispatcher);
		cache::WriteFileAtomically(
			cache_path,
			std::span<std::byte const>(
				reinterpret_cast<std::byte const*>(updated_cache.data()),
				updated_cache.size()
			)
		);
		return pipeline;
	}

	Backend::Pipeline Backend::CreateComputePipeline(
		Backend::LogicalDevice const& ld,
		ComputePipelineDescriptor const& descriptor
	) {
		auto properties = ld.phys_dev.impl->getProperties(*ld.dispatcher);
		auto spirv_profile = SelectSPIRVProfile(properties.apiVersion);
		slang::TargetDesc target{ .format = SLANG_SPIRV };
		target.profile = shader::SlangGlobalSession()->findProfile(spirv_profile.data());
		auto cache_tag = std::format(
			"vulkan-compute-{:04x}-{:04x}-{:08x}-api-{:08x}-{}",
			properties.vendorID,
			properties.deviceID,
			properties.driverVersion,
			properties.apiVersion,
			spirv_profile
		);
		shader::SlangProgram program(target, descriptor.program, cache_tag);
		auto pipeline = CreateVulkanPipelineInterface(ld, program, vk::PipelineBindPoint::eCompute);
		vk::SharedShaderModule module;
		std::optional<vk::PipelineShaderStageCreateInfo> stage;
		for (auto const& entry : program.EntryPoints()) {
			if (entry.stage != PipelineStage::Compute) {
				throw std::invalid_argument("Vulkan compute pipelines accept only a compute entry point");
			}
			if (stage) {
				throw std::invalid_argument("Vulkan compute pipelines require exactly one compute entry point");
			}
			if (entry.code.size() % sizeof(std::uint32_t) != 0u) {
				throw std::runtime_error("Slang returned misaligned SPIR-V bytecode");
			}
			vk::ShaderModuleCreateInfo module_info(
				{},
				entry.code.size(),
				reinterpret_cast<std::uint32_t const*>(entry.code.data())
			);
			auto raw_module = ld.impl->createShaderModule(module_info, nullptr, *ld.dispatcher);
			module = vk::SharedShaderModule(raw_module, ld.impl, { nullptr, *ld.dispatcher });
			stage.emplace(
				vk::PipelineShaderStageCreateFlags{},
				vk::ShaderStageFlagBits::eCompute,
				raw_module,
				entry.name.c_str()
			);
		}
		if (!stage) {
			throw std::invalid_argument("Vulkan compute pipelines require one compute entry point");
		}

		auto cache_path = cache::GetCacheFilePath(std::format(
			"vulkan-pipeline-{:04x}-{:04x}-{:08x}.bin",
			properties.vendorID,
			properties.deviceID,
			properties.driverVersion
		));
		auto cache_data = cache::ReadFile(cache_path);
		vk::PipelineCacheCreateInfo cache_info({}, cache_data.size(), cache_data.data());
		auto raw_cache = ld.impl->createPipelineCache(cache_info, nullptr, *ld.dispatcher);
		vk::SharedPipelineCache pipeline_cache(raw_cache, ld.impl, { nullptr, *ld.dispatcher });
		vk::ComputePipelineCreateInfo info({}, *stage, *pipeline.layout);
		auto creation = ld.impl->createComputePipeline(raw_cache, info, nullptr, *ld.dispatcher);
		pipeline.impl = vk::SharedPipeline(creation.value, ld.impl, { nullptr, *ld.dispatcher });
		auto updated_cache = ld.impl->getPipelineCacheData(raw_cache, *ld.dispatcher);
		cache::WriteFileAtomically(
			cache_path,
			std::span<std::byte const>(
				reinterpret_cast<std::byte const*>(updated_cache.data()),
				updated_cache.size()
			)
		);
		return pipeline;
	}

	Backend::PipelineResourceGroup Backend::CreatePipelineResourceGroup(
		Backend::LogicalDevice const& ld,
		Backend::Pipeline const& pipeline,
		std::uint32_t space,
		std::span<NativePipelineResourceBinding<Backend> const> bindings
	) {
		if (space >= pipeline.descriptor_set_layouts.size()) {
			throw std::out_of_range("Vulkan pipeline resource group space is out of range");
		}
		auto native = MakePipelineResourceGroup<Backend>(pipeline.bindings, space, bindings);
		std::vector<vk::DescriptorPoolSize> pool_sizes;
		for (auto const& binding : native.layout) {
			auto type = MapPipelineDescriptorType(binding.flags);
			auto MatchesType = [type](vk::DescriptorPoolSize const& value) {
				return value.type == type;
			};
			auto existing = std::ranges::find_if(pool_sizes, MatchesType);
			if (existing == pool_sizes.end()) {
				pool_sizes.emplace_back(type, binding.count);
			}
			else {
				existing->descriptorCount += binding.count;
			}
		}

		vk::DescriptorPoolCreateInfo pool_info({}, 1u, pool_sizes);
		auto raw_pool = ld.impl->createDescriptorPool(pool_info, nullptr, *ld.dispatcher);
		vk::SharedDescriptorPool descriptor_pool(raw_pool, ld.impl, { nullptr, *ld.dispatcher });
		auto raw_layout = *pipeline.descriptor_set_layouts[space];
		vk::DescriptorSetAllocateInfo allocate_info(raw_pool, 1u, &raw_layout);
		auto descriptor_sets = ld.impl->allocateDescriptorSets(allocate_info, *ld.dispatcher);

		std::vector<vk::DescriptorBufferInfo> buffer_infos;
		std::vector<vk::DescriptorImageInfo> image_infos;
		std::vector<vk::WriteDescriptorSet> writes;
		buffer_infos.reserve(native.bindings.size());
		image_infos.reserve(native.bindings.size());
		writes.reserve(native.bindings.size());
		for (auto const& resource_binding : native.bindings) {
			auto MatchesBinding = [&resource_binding](PipelineBindingMetadata const& value) {
				return value.slot == resource_binding.slot;
			};
			auto metadata = std::ranges::find_if(native.layout, MatchesBinding);
			auto type = MapPipelineDescriptorType(metadata->flags);
			vk::WriteDescriptorSet write;
			write.dstSet = descriptor_sets.front();
			write.dstBinding = resource_binding.slot;
			write.dstArrayElement = resource_binding.array_element;
			write.descriptorCount = 1u;
			write.descriptorType = type;
			if (auto buffer = std::get_if<NativePipelineBufferBinding<Backend>>(&resource_binding.value)) {
				auto const& resource = std::get<Backend::Resource::Buffer>(buffer->impl.get().impl);
				auto range = buffer->size == PipelineWholeBuffer ? VK_WHOLE_SIZE : buffer->size;
				buffer_infos.emplace_back(resource.vk_handle, buffer->offset, range);
				write.pBufferInfo = &buffer_infos.back();
			}
			else if (auto view = std::get_if<NativePipelineViewBinding<Backend>>(&resource_binding.value)) {
				auto const& texture = std::get<Backend::View::Texture>(view->impl.get().impl);
				auto layout = type == vk::DescriptorType::eStorageImage
					? vk::ImageLayout::eGeneral
					: vk::ImageLayout::eShaderReadOnlyOptimal;
				image_infos.emplace_back(vk::Sampler{}, *texture.impl, layout);
				write.pImageInfo = &image_infos.back();
			}
			else if (auto sampler = std::get_if<NativePipelineSamplerBinding<Backend>>(&resource_binding.value)) {
				image_infos.emplace_back(*sampler->impl.get(), vk::ImageView{}, vk::ImageLayout::eUndefined);
				write.pImageInfo = &image_infos.back();
			}
			else if (auto combined = std::get_if<NativePipelineCombinedBinding<Backend>>(&resource_binding.value)) {
				auto const& texture = std::get<Backend::View::Texture>(combined->view.get().impl);
				image_infos.emplace_back(
					*combined->sampler.get(),
					*texture.impl,
					vk::ImageLayout::eShaderReadOnlyOptimal
				);
				write.pImageInfo = &image_infos.back();
			}
			writes.emplace_back(write);
		}
		std::array<vk::CopyDescriptorSet, 0u> copies;
		ld.impl->updateDescriptorSets(writes, copies, *ld.dispatcher);
		return {
			.space = space,
			.pool = descriptor_pool,
			.set = descriptor_sets.front(),
			.layout = pipeline.layout
		};
	}

	Backend::Scheduler Backend::CreateScheduler(
		LogicalDevice& ld,
		SchedulerDescriptor const& descriptor
	) {
		using Flag = SchedulerFlagBits;
		VulkanScheduler::QueueCollection queues;
		auto priority = GetSchedulerQueuePriority(descriptor.priority);
		if (descriptor.flags.Test(Flag::Graphics)) {
			queues.graphics = CreateSchedulerQueue(ld, CommandQueueType::Graphics, priority);
		}
		if (descriptor.flags.Test(Flag::Compute)) {
			queues.compute = CreateSchedulerQueue(ld, CommandQueueType::Compute, priority);
		}
		if (descriptor.flags.Test(Flag::Copy)) {
			queues.copy = CreateSchedulerQueue(ld, CommandQueueType::Copy, priority);
		}
		if (!queues.graphics && !queues.compute && !queues.copy) {
			throw std::invalid_argument("CreateScheduler(): no execution capability was requested");
		}
		if (queues.graphics && queues.compute &&
			queues.graphics->family == queues.compute->family &&
			queues.graphics->index == queues.compute->index) {
			queues.compute = queues.graphics;
		}
		if (queues.graphics && queues.copy &&
			queues.graphics->family == queues.copy->family &&
			queues.graphics->index == queues.copy->index) {
			queues.copy = queues.graphics;
		}
		else if (queues.compute && queues.copy &&
			queues.compute->family == queues.copy->family &&
			queues.compute->index == queues.copy->index) {
			queues.copy = queues.compute;
		}
		auto scheduler = std::make_shared<VulkanScheduler>();
		scheduler->impl = std::make_shared<VulkanScheduler::Implementation>(
			queues,
			ld.phys_dev,
			ld.presentation_cache,
			std::make_shared<VulkanScheduler::BinarySynchronizationPool>(
				ld.impl,
				ld.dispatcher
			),
			std::unordered_set<std::string>(
				ld.enabled_extensions.begin(),
				ld.enabled_extensions.end()
			),
			ld.enabled_features,
			ld.completion_service
		);
		return scheduler;
	}

	Backend::CommandGraph Backend::CreateCommandGraph(
		execution::CommandGraphDescriptor const& descriptor,
		execution::NativeCommandGraphBindings<Backend> const& bindings
	) {
		return execution::MakeCommandGraph<Backend>(descriptor, bindings);
	}

}
#endif // !defined(__APPLE__)
