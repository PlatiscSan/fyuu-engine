module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <utility>

#include <array>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__)
#include <glad/glad.h>
#endif // !defined(__APPLE__)

module fyuu_rhi:opengl_utility;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource;
import :sampler;

namespace fyuu_rhi::opengl {

	GLenum InternalFormat(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = ResourceFlagBits;
		static constexpr std::array formats{
			std::pair{ Bits::R8Unorm, GL_R8 },
			std::pair{ Bits::R8Snorm, GL_R8_SNORM },
			std::pair{ Bits::R8Uint, GL_R8UI },
			std::pair{ Bits::R8Sint, GL_R8I },
			std::pair{ Bits::R8G8Unorm, GL_RG8 },
			std::pair{ Bits::R8G8Snorm, GL_RG8_SNORM },
			std::pair{ Bits::R8G8Uint, GL_RG8UI },
			std::pair{ Bits::R8G8Sint, GL_RG8I },
			std::pair{ Bits::R8G8B8A8Unorm, GL_RGBA8 },
			std::pair{ Bits::R8G8B8A8Snorm, GL_RGBA8_SNORM },
			std::pair{ Bits::R8G8B8A8Uint, GL_RGBA8UI },
			std::pair{ Bits::R8G8B8A8Sint, GL_RGBA8I },
			std::pair{ Bits::R8G8B8A8Srgb, GL_SRGB8_ALPHA8 },
			std::pair{ Bits::B8G8R8A8Srgb, GL_SRGB8_ALPHA8 },
			std::pair{ Bits::R16Unorm, GL_R16 },
			std::pair{ Bits::R16Snorm, GL_R16_SNORM },
			std::pair{ Bits::R16Uint, GL_R16UI },
			std::pair{ Bits::R16Sint, GL_R16I },
			std::pair{ Bits::R16Float, GL_R16F },
			std::pair{ Bits::R16G16Unorm, GL_RG16 },
			std::pair{ Bits::R16G16Snorm, GL_RG16_SNORM },
			std::pair{ Bits::R16G16Uint, GL_RG16UI },
			std::pair{ Bits::R16G16Sint, GL_RG16I },
			std::pair{ Bits::R16G16Float, GL_RG16F },
			std::pair{ Bits::R16G16B16A16Unorm, GL_RGBA16 },
			std::pair{ Bits::R16G16B16A16Snorm, GL_RGBA16_SNORM },
			std::pair{ Bits::R16G16B16A16Uint, GL_RGBA16UI },
			std::pair{ Bits::R16G16B16A16Sint, GL_RGBA16I },
			std::pair{ Bits::R16G16B16A16Float, GL_RGBA16F },
			std::pair{ Bits::R32Uint, GL_R32UI },
			std::pair{ Bits::R32Sint, GL_R32I },
			std::pair{ Bits::R32Float, GL_R32F },
			std::pair{ Bits::R32G32Uint, GL_RG32UI },
			std::pair{ Bits::R32G32Sint, GL_RG32I },
			std::pair{ Bits::R32G32Float, GL_RG32F },
			std::pair{ Bits::R32G32B32A32Uint, GL_RGBA32UI },
			std::pair{ Bits::R32G32B32A32Sint, GL_RGBA32I },
			std::pair{ Bits::R32G32B32A32Float, GL_RGBA32F },
			std::pair{ Bits::R10G10B10A2Unorm, GL_RGB10_A2 },
			std::pair{ Bits::R10G10B10A2Uint, GL_RGB10_A2UI },
			std::pair{ Bits::R11G11B10Float, GL_R11F_G11F_B10F },
			std::pair{ Bits::R9G9B9E5SharedExp, GL_RGB9_E5 },
			std::pair{ Bits::D16Unorm, GL_DEPTH_COMPONENT16 },
			std::pair{ Bits::D24UnormS8Uint, GL_DEPTH24_STENCIL8 },
			std::pair{ Bits::D32Float, GL_DEPTH_COMPONENT32F },
			std::pair{ Bits::D32FloatS8X24Uint, GL_DEPTH32F_STENCIL8 },
			std::pair{ Bits::Bc1Unorm, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT },
			std::pair{ Bits::Bc1UnormSrgb, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT },
			std::pair{ Bits::Bc2Unorm, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT },
			std::pair{ Bits::Bc2UnormSrgb, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT },
			std::pair{ Bits::Bc3Unorm, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT },
			std::pair{ Bits::Bc3UnormSrgb, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT },
			std::pair{ Bits::Bc4Unorm, GL_COMPRESSED_RED_RGTC1 },
			std::pair{ Bits::Bc4Snorm, GL_COMPRESSED_SIGNED_RED_RGTC1 },
			std::pair{ Bits::Bc5Unorm, GL_COMPRESSED_RG_RGTC2 },
			std::pair{ Bits::Bc5Snorm, GL_COMPRESSED_SIGNED_RG_RGTC2 },
			std::pair{ Bits::Bc6HUfloat, GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT },
			std::pair{ Bits::Bc6HSfloat, GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT },
			std::pair{ Bits::Bc7Unorm, GL_COMPRESSED_RGBA_BPTC_UNORM },
			std::pair{ Bits::Bc7UnormSrgb, GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM }
		};
		GLenum result = 0u;
		for (auto const& [flag, format] : formats) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (result != 0u) {
				throw std::invalid_argument("An OpenGL texture requires exactly one format");
			}
			result = format;
		}
		if (result == 0u) {
			throw std::invalid_argument("An OpenGL texture requires a format");
		}
		return result;
	}

	GLenum TextureViewTarget(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = ResourceFlagBits;
		static constexpr std::array targets{
			std::pair{ Bits::TextureView1D, GL_TEXTURE_1D },
			std::pair{ Bits::TextureView2D, GL_TEXTURE_2D },
			std::pair{ Bits::TextureView2DArray, GL_TEXTURE_2D_ARRAY },
			std::pair{ Bits::TextureViewCube, GL_TEXTURE_CUBE_MAP },
			std::pair{ Bits::TextureViewCubeArray, GL_TEXTURE_CUBE_MAP_ARRAY },
			std::pair{ Bits::TextureView3D, GL_TEXTURE_3D }
		};
		GLenum result = GL_TEXTURE_2D;
		bool found = false;
		for (auto const& [flag, target] : targets) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (found) {
				throw std::invalid_argument(
					"An OpenGL texture view requires exactly one texture view type"
				);
			}
			found = true;
			result = target;
		}
		return result;
	}

	GLenum SamplerAddressMode(AddressMode mode) noexcept {
		switch (mode) {
		case AddressMode::ClampToEdge:
			return GL_CLAMP_TO_EDGE;
		case AddressMode::Repeat:
			return GL_REPEAT;
		case AddressMode::MirroredRepeat:
			return GL_MIRRORED_REPEAT;
		default:
			return GL_CLAMP_TO_EDGE;
		}
	}

	GLenum Filter(FilterMode mode) noexcept {
		switch (mode) {
		case FilterMode::Nearest:
			return GL_NEAREST;
		case FilterMode::Linear:
			return GL_LINEAR;
		default:
			return GL_NEAREST;
		}
	}

	GLenum ComparisonFunction(CompareFunction func) noexcept {
		switch (func) {
		case CompareFunction::Never:
			return GL_NEVER;
		case CompareFunction::Less:
			return GL_LESS;
		case CompareFunction::Equal:
			return GL_EQUAL;
		case CompareFunction::LessEqual:
			return GL_LEQUAL;
		case CompareFunction::Greater:
			return GL_GREATER;
		case CompareFunction::NotEqual:
			return GL_NOTEQUAL;
		case CompareFunction::GreaterEqual:
			return GL_GEQUAL;
		case CompareFunction::Always:
			return GL_ALWAYS;
		default:
			return GL_NONE;
		}
	}

} // namespace fyuu_rhi::opengl
#endif // !defined(__APPLE__)
