module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <utility>

#include <cstdint>
#include <array>
#endif // !defined(__cpp_lib_modules)
#include <dawn/webgpu_cpp.h>

module fyuu_rhi:webgpu_utility;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource;
import :sampler;

namespace fyuu_rhi::webgpu {

	wgpu::BufferUsage BufferUsage(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		wgpu::BufferUsage result{};
		if (flags.Test(Bits::CopySRC)) {
			result |= wgpu::BufferUsage::CopySrc;
		}
		if (flags.Test(Bits::CopyDST)) {
			result |= wgpu::BufferUsage::CopyDst;
		}
		if (flags.Test(Bits::UniformTexelBuffer)) {
			result |= wgpu::BufferUsage::TexelBuffer;
		}
		if (flags.Test(Bits::StorageTexelBuffer)) {
			result |= wgpu::BufferUsage::TexelBuffer;
		}
		if (flags.Test(Bits::UniformBuffer)) {
			result |= wgpu::BufferUsage::Uniform;
		}
		if (flags.Test(Bits::StorageBuffer)) {
			result |= wgpu::BufferUsage::Storage;
		}
		if (flags.Test(Bits::IndexBuffer)) {
			result |= wgpu::BufferUsage::Index;
		}
		if (flags.Test(Bits::VertexBuffer)) {
			result |= wgpu::BufferUsage::Vertex;
		}
		if (flags.Test(Bits::IndirectBuffer)) {
			result |= wgpu::BufferUsage::Indirect;
		}
		if (flags.TestMultipleInRange(Bits::DeviceLocal, Bits::DeviceReadback)) {
			throw std::invalid_argument("A WebGPU buffer cannot request multiple memory access policies");
		}

		// WebGPU separates GPU-resident buffers from mappable ones: a buffer with
		// shader usage (Vertex/Index/Uniform/Storage/...) cannot also be mappable.
		// If shader usage is requested the buffer is device-local; host data must
		// arrive through a separate staging buffer via CopyBufferToBuffer.
		bool has_gpu_usage =
			flags.Test(Bits::UniformTexelBuffer) ||
			flags.Test(Bits::StorageTexelBuffer) ||
			flags.Test(Bits::UniformBuffer) ||
			flags.Test(Bits::StorageBuffer) ||
			flags.Test(Bits::IndexBuffer) ||
			flags.Test(Bits::VertexBuffer) ||
			flags.Test(Bits::IndirectBuffer);
		if (!has_gpu_usage) {
			if (flags.Test(Bits::HostVisible)) {
				result |= wgpu::BufferUsage::MapWrite;
				// WebGPU: a MapWrite buffer may only also be CopySrc.
				result &= wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc;
			}
			else if (flags.Test(Bits::DeviceReadback)) {
				result |= wgpu::BufferUsage::MapRead;
			}
		}
		return result;
	}

	wgpu::TextureUsage TextureUsage(fyuu_rhi::ResourceFlags const& flags) noexcept {
		using Bits = fyuu_rhi::ResourceFlagBits;
		wgpu::TextureUsage result{};
		if (flags.Test(Bits::CopySRC)) {
			result |= wgpu::TextureUsage::CopySrc;
		}
		if (flags.Test(Bits::CopyDST)) {
			result |= wgpu::TextureUsage::CopyDst;
		}
		if (flags.Test(Bits::TextureBinding)) {
			result |= wgpu::TextureUsage::TextureBinding;
		}
		if (flags.Test(Bits::StorageBinding)) {
			result |= wgpu::TextureUsage::StorageBinding;
		}
		if (flags.Test(Bits::RenderAttachment)) {
			result |= wgpu::TextureUsage::RenderAttachment;
		}
		if (flags.Test(Bits::TransientAttachment)) {
			result |= wgpu::TextureUsage::TransientAttachment;
		}
		if (flags.Test(Bits::StorageAttachment)) {
			result |= wgpu::TextureUsage::StorageAttachment;
		}
		return result;
	}

	wgpu::TextureDimension TextureDimension(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		static constexpr std::array dimensions{
			std::pair{ Bits::Texture1D, wgpu::TextureDimension::e1D },
			std::pair{ Bits::Texture2D, wgpu::TextureDimension::e2D },
			std::pair{ Bits::Texture3D, wgpu::TextureDimension::e3D }
		};
		wgpu::TextureDimension result = wgpu::TextureDimension::e2D;
		bool found = false;
		for (auto const& [flag, dimension] : dimensions) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (found) {
				throw std::invalid_argument(
					"A WebGPU texture requires exactly one dimension"
				);
			}
			found = true;
			result = dimension;
		}
		return result;
	}

	wgpu::TextureFormat ResourceFormat(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		static constexpr std::array formats{
			std::pair{ Bits::R8Unorm, wgpu::TextureFormat::R8Unorm },
			std::pair{ Bits::R8Snorm, wgpu::TextureFormat::R8Snorm },
			std::pair{ Bits::R8Uint, wgpu::TextureFormat::R8Uint },
			std::pair{ Bits::R8Sint, wgpu::TextureFormat::R8Sint },
			std::pair{ Bits::R8G8Unorm, wgpu::TextureFormat::RG8Unorm },
			std::pair{ Bits::R8G8Snorm, wgpu::TextureFormat::RG8Snorm },
			std::pair{ Bits::R8G8Uint, wgpu::TextureFormat::RG8Uint },
			std::pair{ Bits::R8G8Sint, wgpu::TextureFormat::RG8Sint },
			std::pair{ Bits::R8G8B8A8Unorm, wgpu::TextureFormat::RGBA8Unorm },
			std::pair{ Bits::R8G8B8A8Snorm, wgpu::TextureFormat::RGBA8Snorm },
			std::pair{ Bits::R8G8B8A8Uint, wgpu::TextureFormat::RGBA8Uint },
			std::pair{ Bits::R8G8B8A8Sint, wgpu::TextureFormat::RGBA8Sint },
			std::pair{ Bits::R8G8B8A8Srgb, wgpu::TextureFormat::RGBA8UnormSrgb },
			std::pair{ Bits::B8G8R8A8Srgb, wgpu::TextureFormat::BGRA8UnormSrgb },
			std::pair{ Bits::R16Unorm, wgpu::TextureFormat::R16Unorm },
			std::pair{ Bits::R16Snorm, wgpu::TextureFormat::R16Snorm },
			std::pair{ Bits::R16Uint, wgpu::TextureFormat::R16Uint },
			std::pair{ Bits::R16Sint, wgpu::TextureFormat::R16Sint },
			std::pair{ Bits::R16Float, wgpu::TextureFormat::R16Float },
			std::pair{ Bits::R16G16Unorm, wgpu::TextureFormat::RG16Unorm },
			std::pair{ Bits::R16G16Snorm, wgpu::TextureFormat::RG16Snorm },
			std::pair{ Bits::R16G16Uint, wgpu::TextureFormat::RG16Uint },
			std::pair{ Bits::R16G16Sint, wgpu::TextureFormat::RG16Sint },
			std::pair{ Bits::R16G16Float, wgpu::TextureFormat::RG16Float },
			std::pair{ Bits::R16G16B16A16Unorm, wgpu::TextureFormat::RGBA16Unorm },
			std::pair{ Bits::R16G16B16A16Snorm, wgpu::TextureFormat::RGBA16Snorm },
			std::pair{ Bits::R16G16B16A16Uint, wgpu::TextureFormat::RGBA16Uint },
			std::pair{ Bits::R16G16B16A16Sint, wgpu::TextureFormat::RGBA16Sint },
			std::pair{ Bits::R16G16B16A16Float, wgpu::TextureFormat::RGBA16Float },
			std::pair{ Bits::R32Uint, wgpu::TextureFormat::R32Uint },
			std::pair{ Bits::R32Sint, wgpu::TextureFormat::R32Sint },
			std::pair{ Bits::R32Float, wgpu::TextureFormat::R32Float },
			std::pair{ Bits::R32G32Uint, wgpu::TextureFormat::RG32Uint },
			std::pair{ Bits::R32G32Sint, wgpu::TextureFormat::RG32Sint },
			std::pair{ Bits::R32G32Float, wgpu::TextureFormat::RG32Float },
			std::pair{ Bits::R32G32B32A32Uint, wgpu::TextureFormat::RGBA32Uint },
			std::pair{ Bits::R32G32B32A32Sint, wgpu::TextureFormat::RGBA32Sint },
			std::pair{ Bits::R32G32B32A32Float, wgpu::TextureFormat::RGBA32Float },
			std::pair{ Bits::R10G10B10A2Unorm, wgpu::TextureFormat::RGB10A2Unorm },
			std::pair{ Bits::R10G10B10A2Uint, wgpu::TextureFormat::RGB10A2Uint },
			std::pair{ Bits::R11G11B10Float, wgpu::TextureFormat::RG11B10Ufloat },
			std::pair{ Bits::R9G9B9E5SharedExp, wgpu::TextureFormat::RGB9E5Ufloat },
			std::pair{ Bits::D16Unorm, wgpu::TextureFormat::Depth16Unorm },
			std::pair{ Bits::D24UnormS8Uint, wgpu::TextureFormat::Depth24PlusStencil8 },
			std::pair{ Bits::D32Float, wgpu::TextureFormat::Depth32Float },
			std::pair{ Bits::D32FloatS8X24Uint, wgpu::TextureFormat::Depth32FloatStencil8 },
			std::pair{ Bits::Bc1Unorm, wgpu::TextureFormat::BC1RGBAUnorm },
			std::pair{ Bits::Bc1UnormSrgb, wgpu::TextureFormat::BC1RGBAUnormSrgb },
			std::pair{ Bits::Bc2Unorm, wgpu::TextureFormat::BC2RGBAUnorm },
			std::pair{ Bits::Bc2UnormSrgb, wgpu::TextureFormat::BC2RGBAUnormSrgb },
			std::pair{ Bits::Bc3Unorm, wgpu::TextureFormat::BC3RGBAUnorm },
			std::pair{ Bits::Bc3UnormSrgb, wgpu::TextureFormat::BC3RGBAUnormSrgb },
			std::pair{ Bits::Bc4Unorm, wgpu::TextureFormat::BC4RUnorm },
			std::pair{ Bits::Bc4Snorm, wgpu::TextureFormat::BC4RSnorm },
			std::pair{ Bits::Bc5Unorm, wgpu::TextureFormat::BC5RGUnorm },
			std::pair{ Bits::Bc5Snorm, wgpu::TextureFormat::BC5RGSnorm },
			std::pair{ Bits::Bc6HUfloat, wgpu::TextureFormat::BC6HRGBUfloat },
			std::pair{ Bits::Bc6HSfloat, wgpu::TextureFormat::BC6HRGBFloat },
			std::pair{ Bits::Bc7Unorm, wgpu::TextureFormat::BC7RGBAUnorm },
			std::pair{ Bits::Bc7UnormSrgb, wgpu::TextureFormat::BC7RGBAUnormSrgb }
		};
		wgpu::TextureFormat result = wgpu::TextureFormat::Undefined;
		for (auto const& [flag, format] : formats) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (result != wgpu::TextureFormat::Undefined) {
				throw std::invalid_argument("A WebGPU texture requires exactly one format");
			}
			result = format;
		}
		if (result == wgpu::TextureFormat::Undefined) {
			throw std::invalid_argument("A WebGPU texture requires a format");
		}
		return result;
	}

	std::uint32_t SampleCount(fyuu_rhi::ResourceFlags const& flags) {
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
		std::uint32_t result = 1u;
		bool found = false;
		for (auto const& [flag, count] : counts) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (found) {
				throw std::invalid_argument("A WebGPU texture requires at most one sample count");
			}
			found = true;
			result = count;
		}
		return result;
	}

	wgpu::TextureViewDimension TextureViewDimension(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		static constexpr std::array dimensions{
			std::pair{ Bits::TextureView1D, wgpu::TextureViewDimension::e1D },
			std::pair{ Bits::TextureView2D, wgpu::TextureViewDimension::e2D },
			std::pair{ Bits::TextureView2DArray, wgpu::TextureViewDimension::e2DArray },
			std::pair{ Bits::TextureViewCube, wgpu::TextureViewDimension::Cube },
			std::pair{ Bits::TextureViewCubeArray, wgpu::TextureViewDimension::CubeArray },
			std::pair{ Bits::TextureView3D, wgpu::TextureViewDimension::e3D }
		};
		wgpu::TextureViewDimension result = wgpu::TextureViewDimension::e2D;
		bool found = false;
		for (auto const& [flag, dimension] : dimensions) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (found) {
				throw std::invalid_argument(
					"A WebGPU texture view requires exactly one texture view type"
				);
			}
			found = true;
			result = dimension;
		}
		return result;
	}

	wgpu::TextureAspect TextureViewAspect(fyuu_rhi::ResourceFlags const& flags) noexcept {
		using Bits = fyuu_rhi::ResourceFlagBits;
		static constexpr std::array aspects{
			std::pair{ Bits::TextureViewAspectDepthOnly, wgpu::TextureAspect::DepthOnly },
			std::pair{ Bits::TextureViewAspectStencilOnly, wgpu::TextureAspect::StencilOnly },
			std::pair{ Bits::TextureViewAspectPlane0Only, wgpu::TextureAspect::Plane0Only },
			std::pair{ Bits::TextureViewAspectPlane1Only, wgpu::TextureAspect::Plane1Only },
			std::pair{ Bits::TextureViewAspectPlane2Only, wgpu::TextureAspect::Plane2Only }
		};
		for (auto const& [flag, aspect] : aspects) {
			if (flags.Test(flag)) {
				return aspect;
			}
		}
		return wgpu::TextureAspect::All;
	}

	wgpu::AddressMode SamplerAddressMode(AddressMode mode) noexcept {
		switch (mode) {
		case AddressMode::ClampToEdge:
			return wgpu::AddressMode::ClampToEdge;
		case AddressMode::Repeat:
			return wgpu::AddressMode::Repeat;
		case AddressMode::MirroredRepeat:
			return wgpu::AddressMode::MirrorRepeat;
		default:
			return wgpu::AddressMode::ClampToEdge;
		}
	}

	wgpu::FilterMode Filter(FilterMode mode) noexcept {
		switch (mode) {
		case FilterMode::Nearest:
			return wgpu::FilterMode::Nearest;
		case FilterMode::Linear:
			return wgpu::FilterMode::Linear;
		default:
			return wgpu::FilterMode::Linear;
		}
	}

	wgpu::MipmapFilterMode MipmapFilter(MipmapFilterMode mode) noexcept {
		switch (mode) {
		case MipmapFilterMode::Nearest:
			return wgpu::MipmapFilterMode::Nearest;
		case MipmapFilterMode::Linear:
			return wgpu::MipmapFilterMode::Linear;
		default:
			return wgpu::MipmapFilterMode::Linear;
		}
	}

	wgpu::CompareFunction ComparisonFunction(CompareFunction func) noexcept {
		switch (func) {
		case CompareFunction::Never:
			return wgpu::CompareFunction::Never;
		case CompareFunction::Less:
			return wgpu::CompareFunction::Less;
		case CompareFunction::Equal:
			return wgpu::CompareFunction::Equal;
		case CompareFunction::LessEqual:
			return wgpu::CompareFunction::LessEqual;
		case CompareFunction::Greater:
			return wgpu::CompareFunction::Greater;
		case CompareFunction::NotEqual:
			return wgpu::CompareFunction::NotEqual;
		case CompareFunction::GreaterEqual:
			return wgpu::CompareFunction::GreaterEqual;
		case CompareFunction::Always:
			return wgpu::CompareFunction::Always;
		default:
			return wgpu::CompareFunction::Undefined;
		}
	}

} // namespace fyuu_rhi::webgpu
