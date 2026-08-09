module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <limits>

#include <cstdint>
#endif // !defined(__cpp_lib_modules)

export module fyuu_rhi:sampler_types;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
namespace fyuu_rhi {

	export enum class AddressMode : std::uint8_t {
		Unknown = 0,
		ClampToEdge,
		Repeat,
		MirroredRepeat,
	};

	export enum class FilterMode : std::uint8_t {
		Unknown = 0,
		Nearest,
		Linear,
	};

	export enum class MipmapFilterMode : std::uint8_t {
		Unknown = 0,
		Nearest,
		Linear,
	};

	export enum class CompareFunction : std::uint8_t {
		Unknown = 0,
		Never,
		Less,
		Equal,
		LessEqual,
		Greater,
		NotEqual,
		GreaterEqual,
		Always,
	};

	export struct SamplerDescriptor {
		AddressMode address_mode_u = AddressMode::Unknown;
		AddressMode address_mode_v = AddressMode::Unknown;
		AddressMode address_mode_w = AddressMode::Unknown;
		FilterMode mag_filter = FilterMode::Unknown;
		FilterMode min_filter = FilterMode::Unknown;
		MipmapFilterMode mipmap_filter = MipmapFilterMode::Unknown;
		std::uint8_t max_anisotropy = 1u; // 1 means no anisotropic filtering
		CompareFunction compare_function = CompareFunction::Unknown;
		float min_lod = 0.0f;
		float max_lod = std::numeric_limits<float>::max();
	};

}