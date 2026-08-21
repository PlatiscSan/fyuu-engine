module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <utility>
#include <cstdint>

#include <array>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)
#define FYUU_RHI_USE_VULKAN_HEADER
#include <vulkan/vulkan_shared.hpp>
#endif // !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)

module fyuu_rhi:vulkan_utility;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
#if !defined(FYUU_RHI_USE_VULKAN_HEADER)
import vulkan;
#endif // !defined(FYUU_RHI_USE_VULKAN_HEADER)
import :resource;
import :sampler;

namespace fyuu_rhi::vulkan {

	vk::Filter Filter(FilterMode mode) noexcept {
		return mode == FilterMode::Nearest ?
			vk::Filter::eNearest :
			vk::Filter::eLinear;
	}

	vk::SamplerMipmapMode MipmapMode(MipmapFilterMode mode) noexcept {
		return mode == MipmapFilterMode::Nearest ?
			vk::SamplerMipmapMode::eNearest :
			vk::SamplerMipmapMode::eLinear;
	}

	vk::SamplerAddressMode SamplerAddressMode(AddressMode mode) noexcept {
		switch (mode) {
		case AddressMode::Repeat:
			return vk::SamplerAddressMode::eRepeat;
		case AddressMode::MirroredRepeat:
			return vk::SamplerAddressMode::eMirroredRepeat;
		case AddressMode::Unknown:
		case AddressMode::ClampToEdge:
			return vk::SamplerAddressMode::eClampToEdge;
		}
		return vk::SamplerAddressMode::eClampToEdge;
	}

	vk::CompareOp ComparisonOperation(CompareFunction function) noexcept {
		switch (function) {
		case CompareFunction::Never:
			return vk::CompareOp::eNever;
		case CompareFunction::Less:
			return vk::CompareOp::eLess;
		case CompareFunction::Equal:
			return vk::CompareOp::eEqual;
		case CompareFunction::LessEqual:
			return vk::CompareOp::eLessOrEqual;
		case CompareFunction::Greater:
			return vk::CompareOp::eGreater;
		case CompareFunction::NotEqual:
			return vk::CompareOp::eNotEqual;
		case CompareFunction::GreaterEqual:
			return vk::CompareOp::eGreaterOrEqual;
		case CompareFunction::Always:
			return vk::CompareOp::eAlways;
		case CompareFunction::Unknown:
			return vk::CompareOp::eNever;
		}
		return vk::CompareOp::eNever;
	}

	vk::ImageType ImageType(ResourceFlags const& flags) {
		using Bits = ResourceFlagBits;
		static constexpr std::array dimensions{
			std::pair{ Bits::Texture1D, vk::ImageType::e1D },
			std::pair{ Bits::Texture2D, vk::ImageType::e2D },
			std::pair{ Bits::Texture3D, vk::ImageType::e3D }
		};
		vk::ImageType result = vk::ImageType::e2D;
		bool found = false;
		for (auto const& [flag, dimension] : dimensions) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (found) {
				throw std::invalid_argument(
					"A Vulkan texture requires exactly one dimension"
				);
			}
			found = true;
			result = dimension;
		}
		return result;
	}

	vk::Format ResourceFormat(ResourceFlags const& flags) {
		using Bits = ResourceFlagBits;
		static constexpr std::array formats{
			std::pair{ Bits::R8Unorm, vk::Format::eR8Unorm },
			std::pair{ Bits::R8Snorm, vk::Format::eR8Snorm },
			std::pair{ Bits::R8Uint, vk::Format::eR8Uint },
			std::pair{ Bits::R8Sint, vk::Format::eR8Sint },
			std::pair{ Bits::R8G8Unorm, vk::Format::eR8G8Unorm },
			std::pair{ Bits::R8G8Snorm, vk::Format::eR8G8Snorm },
			std::pair{ Bits::R8G8Uint, vk::Format::eR8G8Uint },
			std::pair{ Bits::R8G8Sint, vk::Format::eR8G8Sint },
			std::pair{ Bits::R8G8B8A8Unorm, vk::Format::eR8G8B8A8Unorm },
			std::pair{ Bits::R8G8B8A8Snorm, vk::Format::eR8G8B8A8Snorm },
			std::pair{ Bits::R8G8B8A8Uint, vk::Format::eR8G8B8A8Uint },
			std::pair{ Bits::R8G8B8A8Sint, vk::Format::eR8G8B8A8Sint },
			std::pair{ Bits::R8G8B8A8Srgb, vk::Format::eR8G8B8A8Srgb },
			std::pair{ Bits::B8G8R8A8Srgb, vk::Format::eB8G8R8A8Srgb },
			std::pair{ Bits::R16Unorm, vk::Format::eR16Unorm },
			std::pair{ Bits::R16Snorm, vk::Format::eR16Snorm },
			std::pair{ Bits::R16Uint, vk::Format::eR16Uint },
			std::pair{ Bits::R16Sint, vk::Format::eR16Sint },
			std::pair{ Bits::R16Float, vk::Format::eR16Sfloat },
			std::pair{ Bits::R16G16Unorm, vk::Format::eR16G16Unorm },
			std::pair{ Bits::R16G16Snorm, vk::Format::eR16G16Snorm },
			std::pair{ Bits::R16G16Uint, vk::Format::eR16G16Uint },
			std::pair{ Bits::R16G16Sint, vk::Format::eR16G16Sint },
			std::pair{ Bits::R16G16Float, vk::Format::eR16G16Sfloat },
			std::pair{ Bits::R16G16B16A16Unorm, vk::Format::eR16G16B16A16Unorm },
			std::pair{ Bits::R16G16B16A16Snorm, vk::Format::eR16G16B16A16Snorm },
			std::pair{ Bits::R16G16B16A16Uint, vk::Format::eR16G16B16A16Uint },
			std::pair{ Bits::R16G16B16A16Sint, vk::Format::eR16G16B16A16Sint },
			std::pair{ Bits::R16G16B16A16Float, vk::Format::eR16G16B16A16Sfloat },
			std::pair{ Bits::R32Uint, vk::Format::eR32Uint },
			std::pair{ Bits::R32Sint, vk::Format::eR32Sint },
			std::pair{ Bits::R32Float, vk::Format::eR32Sfloat },
			std::pair{ Bits::R32G32Uint, vk::Format::eR32G32Uint },
			std::pair{ Bits::R32G32Sint, vk::Format::eR32G32Sint },
			std::pair{ Bits::R32G32Float, vk::Format::eR32G32Sfloat },
			std::pair{ Bits::R32G32B32A32Uint, vk::Format::eR32G32B32A32Uint },
			std::pair{ Bits::R32G32B32A32Sint, vk::Format::eR32G32B32A32Sint },
			std::pair{ Bits::R32G32B32A32Float, vk::Format::eR32G32B32A32Sfloat },
			std::pair{ Bits::R10G10B10A2Unorm, vk::Format::eA2R10G10B10UnormPack32 },
			std::pair{ Bits::R10G10B10A2Uint, vk::Format::eA2R10G10B10UintPack32 },
			std::pair{ Bits::R11G11B10Float, vk::Format::eB10G11R11UfloatPack32 },
			std::pair{ Bits::R9G9B9E5SharedExp, vk::Format::eE5B9G9R9UfloatPack32 },
			std::pair{ Bits::D16Unorm, vk::Format::eD16Unorm },
			std::pair{ Bits::D24UnormS8Uint, vk::Format::eD24UnormS8Uint },
			std::pair{ Bits::D32Float, vk::Format::eD32Sfloat },
			std::pair{ Bits::D32FloatS8X24Uint, vk::Format::eD32SfloatS8Uint },
			std::pair{ Bits::Bc1Unorm, vk::Format::eBc1RgbaUnormBlock },
			std::pair{ Bits::Bc1UnormSrgb, vk::Format::eBc1RgbaSrgbBlock },
			std::pair{ Bits::Bc2Unorm, vk::Format::eBc2UnormBlock },
			std::pair{ Bits::Bc2UnormSrgb, vk::Format::eBc2SrgbBlock },
			std::pair{ Bits::Bc3Unorm, vk::Format::eBc3UnormBlock },
			std::pair{ Bits::Bc3UnormSrgb, vk::Format::eBc3SrgbBlock },
			std::pair{ Bits::Bc4Unorm, vk::Format::eBc4UnormBlock },
			std::pair{ Bits::Bc4Snorm, vk::Format::eBc4SnormBlock },
			std::pair{ Bits::Bc5Unorm, vk::Format::eBc5UnormBlock },
			std::pair{ Bits::Bc5Snorm, vk::Format::eBc5SnormBlock },
			std::pair{ Bits::Bc6HUfloat, vk::Format::eBc6HUfloatBlock },
			std::pair{ Bits::Bc6HSfloat, vk::Format::eBc6HSfloatBlock },
			std::pair{ Bits::Bc7Unorm, vk::Format::eBc7UnormBlock },
			std::pair{ Bits::Bc7UnormSrgb, vk::Format::eBc7SrgbBlock }
		};
		vk::Format result = vk::Format::eUndefined;
		for (auto const& [flag, format] : formats) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (result != vk::Format::eUndefined) {
				throw std::invalid_argument("A Vulkan texture requires exactly one format");
			}
			result = format;
		}
		if (result == vk::Format::eUndefined) {
			throw std::invalid_argument("A Vulkan texture requires a format");
		}
		return result;
	}

	bool IsDepthStencilFormat(vk::Format format) noexcept {
		return format == vk::Format::eD16Unorm ||
			format == vk::Format::eD24UnormS8Uint ||
			format == vk::Format::eD32Sfloat ||
			format == vk::Format::eD32SfloatS8Uint;
	}

	vk::SampleCountFlagBits SampleCount(ResourceFlags const& flags) {
		using Bits = ResourceFlagBits;
		static constexpr std::array counts{
			std::pair{ Bits::Sample1, vk::SampleCountFlagBits::e1 },
			std::pair{ Bits::Sample2, vk::SampleCountFlagBits::e2 },
			std::pair{ Bits::Sample4, vk::SampleCountFlagBits::e4 },
			std::pair{ Bits::Sample8, vk::SampleCountFlagBits::e8 },
			std::pair{ Bits::Sample16, vk::SampleCountFlagBits::e16 },
			std::pair{ Bits::Sample32, vk::SampleCountFlagBits::e32 },
			std::pair{ Bits::Sample64, vk::SampleCountFlagBits::e64 }
		};
		vk::SampleCountFlagBits result = vk::SampleCountFlagBits::e1;
		bool found = false;
		for (auto const& [flag, count] : counts) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (found) {
				throw std::invalid_argument("A Vulkan texture requires at most one sample count");
			}
			found = true;
			result = count;
		}
		return result;
	}

	vk::ImageTiling ImageTiling(ResourceFlags const& flags) {
		using Bits = ResourceFlagBits;
		if (flags.TestMultipleInRange(Bits::DeviceLocal, Bits::DeviceReadback)) {
			throw std::invalid_argument("A Vulkan texture requires at most one memory access policy");
		}
		return flags.Test(Bits::HostVisible) || flags.Test(Bits::DeviceReadback) ? 
			vk::ImageTiling::eLinear : 
			vk::ImageTiling::eOptimal;
	}

	vk::ImageUsageFlags ImageUsage(ResourceFlags const& flags, vk::Format format) {
		using Bits = ResourceFlagBits;
		vk::ImageUsageFlags result;
		if (flags.Test(Bits::CopySRC)) {
			result |= vk::ImageUsageFlagBits::eTransferSrc;
		}
		if (flags.Test(Bits::CopyDST)) {
			result |= vk::ImageUsageFlagBits::eTransferDst;
		}
		if (flags.Test(Bits::TextureBinding)) {
			result |= vk::ImageUsageFlagBits::eSampled;
		}
		if (flags.Test(Bits::StorageBinding)) {
			result |= vk::ImageUsageFlagBits::eStorage;
		}
		if (flags.Test(Bits::RenderAttachment)) {
			result |= IsDepthStencilFormat(format) ?
				vk::ImageUsageFlagBits::eDepthStencilAttachment :
				vk::ImageUsageFlagBits::eColorAttachment;
		}
		if (flags.Test(Bits::TransientAttachment)) {
			result |= vk::ImageUsageFlagBits::eTransientAttachment;
		}
		if (flags.Test(Bits::StorageAttachment)) {
			result |= vk::ImageUsageFlagBits::eInputAttachment;
		}
		return result;
	}

	vk::ImageViewType ImageViewType(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = ResourceFlagBits;
		static constexpr std::array types{
			std::pair{ Bits::TextureView1D, vk::ImageViewType::e1D },
			std::pair{ Bits::TextureView2D, vk::ImageViewType::e2D },
			std::pair{ Bits::TextureView3D, vk::ImageViewType::e3D },
			std::pair{ Bits::TextureViewCube, vk::ImageViewType::eCube },
			std::pair{ Bits::TextureView2DArray, vk::ImageViewType::e2DArray },
			std::pair{ Bits::TextureViewCubeArray, vk::ImageViewType::eCubeArray }
		};
		vk::ImageViewType result = vk::ImageViewType::e2D;
		bool found = false;
		for (auto const& [flag, type] : types) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (found) {
				throw std::invalid_argument("A Vulkan image view requires exactly one texture view type");
			}
			found = true;
			result = type;
		}
		return result;
	}

	// The plane count of a Vulkan format is a static property of the format enum
	// (VK_KHR_sampler_ycbcr_conversion). Whether the device actually supports
	// disjoint sharing for it is validated at image creation time instead.
	std::uint32_t MultiPlaneCount(vk::Format format) noexcept {
		switch (format) {
		// 2-plane formats
		case vk::Format::eG8B8G8R8422Unorm:
		case vk::Format::eG8B8R82Plane420Unorm:
		case vk::Format::eG8B8R82Plane422Unorm:
		case vk::Format::eG8B8R82Plane444Unorm:
		case vk::Format::eG10X6B10X6R10X62Plane420Unorm3Pack16:
		case vk::Format::eG10X6B10X6R10X62Plane422Unorm3Pack16:
		case vk::Format::eG10X6B10X6R10X62Plane444Unorm3Pack16:
		case vk::Format::eG12X4B12X4R12X42Plane420Unorm3Pack16:
		case vk::Format::eG12X4B12X4R12X42Plane422Unorm3Pack16:
		case vk::Format::eG12X4B12X4R12X42Plane444Unorm3Pack16:
		case vk::Format::eG16B16R162Plane420Unorm:
		case vk::Format::eG16B16R162Plane422Unorm:
		case vk::Format::eG16B16R162Plane444Unorm:
			return 2u;
		// 3-plane formats
		case vk::Format::eG8B8R83Plane420Unorm:
		case vk::Format::eG8B8R83Plane422Unorm:
		case vk::Format::eG8B8R83Plane444Unorm:
		case vk::Format::eG10X6B10X6R10X63Plane420Unorm3Pack16:
		case vk::Format::eG10X6B10X6R10X63Plane422Unorm3Pack16:
		case vk::Format::eG10X6B10X6R10X63Plane444Unorm3Pack16:
		case vk::Format::eG12X4B12X4R12X43Plane420Unorm3Pack16:
		case vk::Format::eG12X4B12X4R12X43Plane422Unorm3Pack16:
		case vk::Format::eG12X4B12X4R12X43Plane444Unorm3Pack16:
		case vk::Format::eG16B16R163Plane420Unorm:
		case vk::Format::eG16B16R163Plane422Unorm:
		case vk::Format::eG16B16R163Plane444Unorm:
			return 3u;
		default:
			return 0u;
		}
	}

	vk::ImageAspectFlags ImageAspect(fyuu_rhi::ResourceFlags const& flags, vk::Format format) {
		using Bits = ResourceFlagBits;
		bool plane0 = flags.Test(Bits::TextureViewAspectPlane0Only);
		bool plane1 = flags.Test(Bits::TextureViewAspectPlane1Only);
		bool plane2 = flags.Test(Bits::TextureViewAspectPlane2Only);
		if (plane0 || plane1 || plane2) {
			// At most one plane aspect may be requested; the plane count is a format
			// constant, and disjoint device support is validated at image creation.
			std::uint32_t plane_flags_count =
				(plane0 ? 1u : 0u) + (plane1 ? 1u : 0u) + (plane2 ? 1u : 0u);
			if (plane_flags_count != 1u) {
				throw std::invalid_argument(
					"A Vulkan plane aspect requires exactly one plane"
				);
			}
			std::uint32_t requested_plane = plane0 ? 0u : plane1 ? 1u : 2u;
			auto plane_count = MultiPlaneCount(format);
			if (plane_count == 0u) {
				throw std::invalid_argument("A Vulkan plane aspect requires a multiplanar format");
			}
			if (requested_plane >= plane_count) {
				throw std::invalid_argument("A Vulkan plane aspect exceeds the format's plane count");
			}
			switch (requested_plane) {
			case 0u:
				return vk::ImageAspectFlagBits::ePlane0;
			case 1u:
				return vk::ImageAspectFlagBits::ePlane1;
			default:
				return vk::ImageAspectFlagBits::ePlane2;
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
		// Depth-only and stencil-only are mutually exclusive; neither set means the
		// combined depth|stencil aspect.
		bool depth_only = flags.Test(Bits::TextureViewAspectDepthOnly);
		bool stencil_only = flags.Test(Bits::TextureViewAspectStencilOnly);
		if (depth_only && stencil_only) {
			throw std::invalid_argument("A Vulkan depth/stencil view cannot be both depth-only and stencil-only");
		}
		if (depth_only) {
			return vk::ImageAspectFlagBits::eDepth;
		}
		if (stencil_only) {
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

} // namespace fyuu_rhi::vulkan
#endif // !defined(__APPLE__)
