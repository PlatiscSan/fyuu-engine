module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <utility>

#include <array>
#include <optional>
#endif // !defined(__cpp_lib_modules)
#if defined(__APPLE__)
#include <Metal/Metal.hpp>
#endif // defined(__APPLE__)

module fyuu_rhi:metal_utility;
#if defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource;
import :sampler;

namespace fyuu_rhi::metal {

	MTL::StorageMode StorageMode(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		if (flags.TestMultipleInRange(Bits::DeviceLocal, Bits::DeviceReadback)) {
			throw std::invalid_argument("A Metal resource cannot request multiple memory access policies");
		}
		if (flags.Test(Bits::HostVisible) || flags.Test(Bits::DeviceReadback)) {
			// Shared storage is CPU- and GPU-visible, so it covers both upload
			// (HostVisible) and readback (DeviceReadback). On macOS, Managed would
			// give higher discrete-GPU bandwidth for readback, but Shared is correct
			// everywhere and avoids a per-platform split.
			return MTL::StorageMode::Shared;
		}
		return MTL::StorageMode::Private;
	}

	MTL::ResourceOptions BufferOptions(fyuu_rhi::ResourceFlags const& flags) noexcept {
		switch (StorageMode(flags)) {
		case MTL::StorageMode::Managed:
			return MTL::ResourceOptions::ResourceStorageModeManaged;
		case MTL::StorageMode::Private:
			return MTL::ResourceOptions::ResourceStorageModePrivate;
		default:
			return MTL::ResourceOptions::ResourceStorageModeShared;
		}
	}

	MTL::PixelFormat PixelFormat(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		static constexpr std::array formats{
			std::pair{ Bits::R8Unorm, MTL::PixelFormat::R8Unorm },
			std::pair{ Bits::R8Snorm, MTL::PixelFormat::R8Snorm },
			std::pair{ Bits::R8Uint, MTL::PixelFormat::R8Uint },
			std::pair{ Bits::R8Sint, MTL::PixelFormat::R8Sint },
			std::pair{ Bits::R8G8Unorm, MTL::PixelFormat::RG8Unorm },
			std::pair{ Bits::R8G8Snorm, MTL::PixelFormat::RG8Snorm },
			std::pair{ Bits::R8G8Uint, MTL::PixelFormat::RG8Uint },
			std::pair{ Bits::R8G8Sint, MTL::PixelFormat::RG8Sint },
			std::pair{ Bits::R8G8B8A8Unorm, MTL::PixelFormat::RGBA8Unorm },
			std::pair{ Bits::R8G8B8A8Snorm, MTL::PixelFormat::RGBA8Snorm },
			std::pair{ Bits::R8G8B8A8Uint, MTL::PixelFormat::RGBA8Uint },
			std::pair{ Bits::R8G8B8A8Sint, MTL::PixelFormat::RGBA8Sint },
			std::pair{ Bits::R8G8B8A8Srgb, MTL::PixelFormat::RGBA8Unorm_sRGB },
			std::pair{ Bits::B8G8R8A8Srgb, MTL::PixelFormat::BGRA8Unorm_sRGB },
			std::pair{ Bits::R16Unorm, MTL::PixelFormat::R16Unorm },
			std::pair{ Bits::R16Snorm, MTL::PixelFormat::R16Snorm },
			std::pair{ Bits::R16Uint, MTL::PixelFormat::R16Uint },
			std::pair{ Bits::R16Sint, MTL::PixelFormat::R16Sint },
			std::pair{ Bits::R16Float, MTL::PixelFormat::R16Float },
			std::pair{ Bits::R16G16Unorm, MTL::PixelFormat::RG16Unorm },
			std::pair{ Bits::R16G16Snorm, MTL::PixelFormat::RG16Snorm },
			std::pair{ Bits::R16G16Uint, MTL::PixelFormat::RG16Uint },
			std::pair{ Bits::R16G16Sint, MTL::PixelFormat::RG16Sint },
			std::pair{ Bits::R16G16Float, MTL::PixelFormat::RG16Float },
			std::pair{ Bits::R16G16B16A16Unorm, MTL::PixelFormat::RGBA16Unorm },
			std::pair{ Bits::R16G16B16A16Snorm, MTL::PixelFormat::RGBA16Snorm },
			std::pair{ Bits::R16G16B16A16Uint, MTL::PixelFormat::RGBA16Uint },
			std::pair{ Bits::R16G16B16A16Sint, MTL::PixelFormat::RGBA16Sint },
			std::pair{ Bits::R16G16B16A16Float, MTL::PixelFormat::RGBA16Float },
			std::pair{ Bits::R32Uint, MTL::PixelFormat::R32Uint },
			std::pair{ Bits::R32Sint, MTL::PixelFormat::R32Sint },
			std::pair{ Bits::R32Float, MTL::PixelFormat::R32Float },
			std::pair{ Bits::R32G32Uint, MTL::PixelFormat::RG32Uint },
			std::pair{ Bits::R32G32Sint, MTL::PixelFormat::RG32Sint },
			std::pair{ Bits::R32G32Float, MTL::PixelFormat::RG32Float },
			std::pair{ Bits::R32G32B32A32Uint, MTL::PixelFormat::RGBA32Uint },
			std::pair{ Bits::R32G32B32A32Sint, MTL::PixelFormat::RGBA32Sint },
			std::pair{ Bits::R32G32B32A32Float, MTL::PixelFormat::RGBA32Float },
			std::pair{ Bits::R10G10B10A2Unorm, MTL::PixelFormat::RGB10A2Unorm },
			std::pair{ Bits::R11G11B10Float, MTL::PixelFormat::RG11B10Float },
			std::pair{ Bits::R9G9B9E5SharedExp, MTL::PixelFormat::RGB9E5Float },
			std::pair{ Bits::D16Unorm, MTL::PixelFormat::Depth16Unorm },
			std::pair{ Bits::D24UnormS8Uint, MTL::PixelFormat::Depth24Unorm_Stencil8 },
			std::pair{ Bits::D32Float, MTL::PixelFormat::Depth32Float },
			std::pair{ Bits::D32FloatS8X24Uint, MTL::PixelFormat::Depth32Float_Stencil8 },
			std::pair{ Bits::Bc1Unorm, MTL::PixelFormat::BC1_RGBA },
			std::pair{ Bits::Bc1UnormSrgb, MTL::PixelFormat::BC1_RGBA_sRGB },
			std::pair{ Bits::Bc2Unorm, MTL::PixelFormat::BC2_RGBA },
			std::pair{ Bits::Bc2UnormSrgb, MTL::PixelFormat::BC2_RGBA_sRGB },
			std::pair{ Bits::Bc3Unorm, MTL::PixelFormat::BC3_RGBA },
			std::pair{ Bits::Bc3UnormSrgb, MTL::PixelFormat::BC3_RGBA_sRGB },
			std::pair{ Bits::Bc4Unorm, MTL::PixelFormat::BC4_RUnorm },
			std::pair{ Bits::Bc4Snorm, MTL::PixelFormat::BC4_RSnorm },
			std::pair{ Bits::Bc5Unorm, MTL::PixelFormat::BC5_RGUnorm },
			std::pair{ Bits::Bc5Snorm, MTL::PixelFormat::BC5_RGSnorm },
			std::pair{ Bits::Bc6HUfloat, MTL::PixelFormat::BC6H_RGBUfloat },
			std::pair{ Bits::Bc6HSfloat, MTL::PixelFormat::BC6H_RGBFloat },
			std::pair{ Bits::Bc7Unorm, MTL::PixelFormat::BC7_RGBAUnorm },
			std::pair{ Bits::Bc7UnormSrgb, MTL::PixelFormat::BC7_RGBAUnorm_sRGB }
		};
		MTL::PixelFormat result = MTL::PixelFormat::Invalid;
		for (auto const& [flag, format] : formats) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (result != MTL::PixelFormat::Invalid) {
				throw std::invalid_argument("A Metal texture requires exactly one format");
			}
			result = format;
		}
		if (result == MTL::PixelFormat::Invalid) {
			throw std::invalid_argument("A Metal texture requires a format");
		}
		return result;
	}

	MTL::TextureType TextureType(fyuu_rhi::ResourceFlags const& flags, std::size_t depth_or_array_layers) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		if (flags.TestMultipleInRange(Bits::Texture1D, Bits::Texture3D)) {
			throw std::invalid_argument("A Metal texture requires exactly one dimension");
		}
		if (flags.Test(Bits::Texture1D)) {
			return depth_or_array_layers > 1u ?
				MTL::TextureType::TextureType1DArray :
				MTL::TextureType::TextureType1D;
		}
		if (flags.Test(Bits::Texture3D)) {
			return MTL::TextureType::TextureType3D;
		}
		return depth_or_array_layers > 1u ?
			MTL::TextureType::TextureType2DArray :
			MTL::TextureType::TextureType2D;
	}

	MTL::TextureUsage TextureUsage(fyuu_rhi::ResourceFlags const& flags) noexcept {
		using Bits = fyuu_rhi::ResourceFlagBits;
		MTL::TextureUsage result = MTL::TextureUsage::ShaderRead;
		if (flags.Test(Bits::StorageBinding)) {
			result |= MTL::TextureUsage::ShaderWrite;
		}
		if (flags.Test(Bits::RenderAttachment) ||
			flags.Test(Bits::TransientAttachment) ||
			flags.Test(Bits::StorageAttachment)) {
			result |= MTL::TextureUsage::RenderTarget;
		}
		return result;
	}

	std::size_t SampleCount(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		static constexpr std::array counts{
			std::pair{ Bits::Sample1, 1u },
			std::pair{ Bits::Sample2, 2u },
			std::pair{ Bits::Sample4, 4u },
			std::pair{ Bits::Sample8, 8u },
			std::pair{ Bits::Sample16, 16u },
			std::pair{ Bits::Sample32, 32u },
			std::pair{ Bits::Sample64, 64u }
		};
		std::size_t result = 1u;
		bool found = false;
		for (auto const& [flag, count] : counts) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (found) {
				throw std::invalid_argument("A Metal texture requires at most one sample count");
			}
			found = true;
			result = count;
		}
		return result;
	}

	std::optional<MTL::TextureType> ViewTextureType(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		// A 2D view needs no special type (the view aliases the texture), so it maps
		// to nullopt. It still occupies a table slot so that a second view-type flag
		// alongside it is detected as a conflict.
		static constexpr std::array types{
			std::pair{ Bits::TextureView1D, std::optional{ MTL::TextureType::TextureType1D } },
			std::pair{ Bits::TextureView2D, std::optional<MTL::TextureType>{} },
			std::pair{ Bits::TextureView2DArray, std::optional{ MTL::TextureType::TextureType2DArray } },
			std::pair{ Bits::TextureViewCube, std::optional{ MTL::TextureType::TextureTypeCube } },
			std::pair{ Bits::TextureViewCubeArray, std::optional{ MTL::TextureType::TextureTypeCubeArray } },
			std::pair{ Bits::TextureView3D, std::optional{ MTL::TextureType::TextureType3D } }
		};
		std::optional<MTL::TextureType> result;
		for (auto const& [flag, type] : types) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (result) {
				throw std::invalid_argument("A Metal texture view requires exactly one view type");
			}
			result = type;
		}
		return result;
	}

	MTL::SamplerAddressMode SamplerAddressMode(AddressMode mode) noexcept {
		switch (mode) {
		case AddressMode::ClampToEdge:
			return MTL::SamplerAddressMode::SamplerAddressModeClampToEdge;
		case AddressMode::Repeat:
			return MTL::SamplerAddressMode::SamplerAddressModeRepeat;
		case AddressMode::MirroredRepeat:
			return MTL::SamplerAddressMode::SamplerAddressModeMirrorRepeat;
		default:
			return MTL::SamplerAddressMode::SamplerAddressModeClampToEdge;
		}
	}

	MTL::SamplerMinMagFilter Filter(FilterMode mode) noexcept {
		switch (mode) {
		case FilterMode::Nearest:
			return MTL::SamplerMinMagFilter::SamplerMinMagFilterNearest;
		case FilterMode::Linear:
			return MTL::SamplerMinMagFilter::SamplerMinMagFilterLinear;
		default:
			return MTL::SamplerMinMagFilter::SamplerMinMagFilterLinear;
		}
	}

	MTL::SamplerMipFilter MipmapFilter(MipmapFilterMode mode) noexcept {
		switch (mode) {
		case MipmapFilterMode::Nearest:
			return MTL::SamplerMipFilter::SamplerMipFilterNearest;
		case MipmapFilterMode::Linear:
			return MTL::SamplerMipFilter::SamplerMipFilterLinear;
		default:
			return MTL::SamplerMipFilter::SamplerMipFilterNotMipmapped;
		}
	}

	MTL::CompareFunction ComparisonFunction(CompareFunction func) noexcept {
		switch (func) {
		case CompareFunction::Never:
			return MTL::CompareFunction::CompareFunctionNever;
		case CompareFunction::Less:
			return MTL::CompareFunction::CompareFunctionLess;
		case CompareFunction::Equal:
			return MTL::CompareFunction::CompareFunctionEqual;
		case CompareFunction::LessEqual:
			return MTL::CompareFunction::CompareFunctionLessEqual;
		case CompareFunction::Greater:
			return MTL::CompareFunction::CompareFunctionGreater;
		case CompareFunction::NotEqual:
			return MTL::CompareFunction::CompareFunctionNotEqual;
		case CompareFunction::GreaterEqual:
			return MTL::CompareFunction::CompareFunctionGreaterEqual;
		case CompareFunction::Always:
			return MTL::CompareFunction::CompareFunctionAlways;
		default:
			return MTL::CompareFunction::CompareFunctionNever;
		}
	}

} // namespace fyuu_rhi::metal
#endif // defined(__APPLE__)
