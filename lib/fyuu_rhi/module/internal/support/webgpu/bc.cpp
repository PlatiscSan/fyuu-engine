module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <stdexcept>

#include <vector>

#include <algorithm>

#include <string>

#include <limits>

#include <cstdint>

#include <mutex>

#include <variant>
#endif
#include <dawn/webgpu_cpp.h>
module fyuu_rhi:webgpu_bc;
#if defined(__cpp_lib_modules)
import std;
#endif
import :webgpu_data;
import :resource;
import :webgpu_bc67;

namespace fyuu_rhi::webgpu {

	/** @brief Identifies an implemented software decoder; zero means not implemented. */
	std::uint32_t SoftwareBCKind(wgpu::TextureFormat format) noexcept {
		switch (format) {
			case wgpu::TextureFormat::BC1RGBAUnorm:
			case wgpu::TextureFormat::BC1RGBAUnormSrgb:
				return 1u;
			case wgpu::TextureFormat::BC2RGBAUnorm:
			case wgpu::TextureFormat::BC2RGBAUnormSrgb:
				return 2u;
			case wgpu::TextureFormat::BC3RGBAUnorm:
			case wgpu::TextureFormat::BC3RGBAUnormSrgb:
				return 3u;
			case wgpu::TextureFormat::BC4RUnorm:
				return 4u;
			case wgpu::TextureFormat::BC4RSnorm:
				return 5u;
			case wgpu::TextureFormat::BC5RGUnorm:
				return 6u;
			case wgpu::TextureFormat::BC5RGSnorm:
				return 7u;
			case wgpu::TextureFormat::BC6HRGBUfloat:
				return 8u;
			case wgpu::TextureFormat::BC6HRGBFloat:
				return 9u;
			case wgpu::TextureFormat::BC7RGBAUnorm:
			case wgpu::TextureFormat::BC7RGBAUnormSrgb:
				return 10u;
			default:
				return 0u;
		}
	}

	/** @brief Returns BC block bytes, or zero for a non-BC format. */
	std::uint32_t BCBlockBytes(wgpu::TextureFormat format) noexcept {
		switch (format) {
			case wgpu::TextureFormat::BC1RGBAUnorm:
			case wgpu::TextureFormat::BC1RGBAUnormSrgb:
			case wgpu::TextureFormat::BC4RUnorm:
			case wgpu::TextureFormat::BC4RSnorm:
				return 8u;
			case wgpu::TextureFormat::BC2RGBAUnorm:
			case wgpu::TextureFormat::BC2RGBAUnormSrgb:
			case wgpu::TextureFormat::BC3RGBAUnorm:
			case wgpu::TextureFormat::BC3RGBAUnormSrgb:
			case wgpu::TextureFormat::BC5RGUnorm:
			case wgpu::TextureFormat::BC5RGSnorm:
			case wgpu::TextureFormat::BC6HRGBUfloat:
			case wgpu::TextureFormat::BC6HRGBFloat:
			case wgpu::TextureFormat::BC7RGBAUnorm:
			case wgpu::TextureFormat::BC7RGBAUnormSrgb:
				return 16u;
			default:
				return 0u;
		}
	}

	/** @brief Normalizes only WebGPU's copy-compatible linear/sRGB format pairs. */
	wgpu::TextureFormat BCCopyFormat(wgpu::TextureFormat format) noexcept {
		switch (format) {
			case wgpu::TextureFormat::BC1RGBAUnormSrgb:
				return wgpu::TextureFormat::BC1RGBAUnorm;
			case wgpu::TextureFormat::BC2RGBAUnormSrgb:
				return wgpu::TextureFormat::BC2RGBAUnorm;
			case wgpu::TextureFormat::BC3RGBAUnormSrgb:
				return wgpu::TextureFormat::BC3RGBAUnorm;
			case wgpu::TextureFormat::BC7RGBAUnormSrgb:
				return wgpu::TextureFormat::BC7RGBAUnorm;
			default:
				return format;
		}
	}

	/** @brief Preserves sRGB conversion, signed channels and HDR precision. */
	wgpu::TextureFormat BCDecodedFormat(wgpu::TextureFormat format) noexcept {
		switch (format) {
			case wgpu::TextureFormat::BC1RGBAUnormSrgb:
			case wgpu::TextureFormat::BC2RGBAUnormSrgb:
			case wgpu::TextureFormat::BC3RGBAUnormSrgb:
			case wgpu::TextureFormat::BC7RGBAUnormSrgb:
				return wgpu::TextureFormat::RGBA8UnormSrgb;
			case wgpu::TextureFormat::BC4RUnorm:
			case wgpu::TextureFormat::BC5RGUnorm:
				return wgpu::TextureFormat::RGBA16Unorm;
			case wgpu::TextureFormat::BC4RSnorm:
			case wgpu::TextureFormat::BC5RGSnorm:
				return wgpu::TextureFormat::RGBA16Snorm;
			case wgpu::TextureFormat::BC6HRGBUfloat:
			case wgpu::TextureFormat::BC6HRGBFloat:
				return wgpu::TextureFormat::RGBA16Float;
			default:
				return wgpu::TextureFormat::RGBA8Unorm;
		}
	}

	struct BCLayout {
		/// @brief Byte offset of the mip's first array slice in original-block storage.
		std::uint64_t offset;
		/// @brief Byte stride of one block row, aligned for native texture copies.
		std::uint32_t pitch;
		/// @brief Block rows per array slice.
		std::uint32_t rows;
		/// @brief Original virtual mip dimensions, not padded scratch dimensions.
		std::uint32_t width;
		std::uint32_t height;
	};

	/** @brief Computes device-copy-aligned block storage for each original mip. */
	BCLayout BCMipLayout(wgpu::Texture const& texture, std::uint32_t bytes, std::uint32_t mip) {
		std::uint64_t offset = 0u;
		for (std::uint32_t level = 0u; level <= mip; ++level) {
			auto width = (std::max)(texture.GetWidth() >> level, 1u);
			auto height = (std::max)(texture.GetHeight() >> level, 1u);
			auto pitch =
			    ((static_cast<std::uint64_t>(width) + 3u) / 4u * bytes + 255u) / 256u * 256u;
			if (pitch > (std::numeric_limits<std::uint32_t>::max)()) {
				throw std::length_error("BC storage row exceeds native limits");
			}
			auto rows = (height + 3u) / 4u;
			if (level == mip) {
				return {offset, static_cast<std::uint32_t>(pitch), rows, width, height};
			}
			offset += pitch * rows * texture.GetDepthOrArrayLayers();
			offset = (offset + 511u) / 512u * 512u;
		}
		throw std::logic_error("Invalid BC mip");
	}

	/** @brief Uses hardware BC decoding when available and software decoding otherwise.
	 * @note The padded scratch image is private. Only the original-sized decoded
	 * image is exposed to user shaders, so their UVs and mip dimensions stay intact.
	 */
	Resource::BCStorage CreateBCStorage(
	    wgpu::Device const& device,
	    wgpu::Texture const& texture,
	    wgpu::TextureFormat format
	) {
		auto layout = BCMipLayout(texture, BCBlockBytes(format), texture.GetMipLevelCount() - 1u);
		bool software = !device.HasFeature(wgpu::FeatureName::TextureCompressionBC);
		wgpu::BufferDescriptor buffer{
		    .usage = wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::CopyDst |
		        (software ? wgpu::BufferUsage::Storage : wgpu::BufferUsage::None),
		    .size = layout.offset +
		        static_cast<std::uint64_t>(layout.pitch) * layout.rows *
		            texture.GetDepthOrArrayLayers()
		};
		wgpu::Limits limits;
		if (!device.GetLimits(&limits) || buffer.size > limits.maxBufferSize) {
			throw std::length_error("BC block storage exceeds WebGPU buffer limits");
		}
		if (software) {
			auto base = BCMipLayout(texture, BCBlockBytes(format), 0u);
			if (static_cast<std::uint64_t>(base.pitch) * base.rows >
			    limits.maxStorageBufferBindingSize) {
				throw std::length_error(
				    "A BC slice exceeds the software decoder storage binding limit"
				);
			}
		}
		if (BCBlockBytes(texture.GetFormat()) != 0u) {
			return {device.CreateBuffer(&buffer), format, {}};
		}
		static constexpr char code[] = R"(
@group(0) @binding(0) var image: texture_2d<f32>;
@group(0) @binding(1) var nearest: sampler;
@vertex fn vs(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    var positions = array<vec2f, 3>(vec2f(-1,-1), vec2f(3,-1), vec2f(-1,3));
    return vec4f(positions[index], 0, 1);
}
@fragment fn fs(@builtin(position) position: vec4f) -> @location(0) vec4f {
    return textureSampleLevel(image, nearest, position.xy / vec2f(textureDimensions(image)), 0);
}
)";
		/** @brief Decode directly from retained block bytes, without a compressed
		 * texture or CPU readback. All endpoint expansion and interpolation is integer.
		 * sRGB bytes are copied from a linear render target; sampling performs native
		 * sRGB conversion afterwards, avoiding compatibility-mode view reinterpretation.
		 * @note NVIDIA/Intel BC4/BC5 use normalized16 storage, not half floats;
		 * AMD's normalized14 values require float32 storage. The normalized
		 * helper constructs the nearest-even IEEE-754 result using integer division;
		 * ordinary WGSL floating division can introduce a reciprocal-rounding error.
		 * Storage precision and channel count must match the tested sampling profile;
		 * equal nearest-sampled values alone do not establish filtered equivalence.
		 */
		static constexpr char software_code[] = R"(
struct Parameters { row_words: u32, kind: u32, profile: u32, padding: u32 }
@group(0) @binding(0) var<storage, read> blocks: array<u32>;
@group(0) @binding(1) var<uniform> parameters: Parameters;
fn bits(lo: u32, hi: u32, start: u32, count: u32) -> u32 {
    var value: u32;
    if (start >= 32u) { value = hi >> (start - 32u); }
    else {
        value = lo >> start;
        if (start + count > 32u) { value |= hi << (32u - start); }
    }
    return value & ((1u << count) - 1u);
}
fn normalized(value: i32, denominator: u32) -> f32 {
    if (value == 0) { return 0.0; }
    var numerator = u32(abs(value));
    var exponent = 0;
    for (var i = 0u; i < 16u && numerator < denominator; i++) {
        numerator *= 2u;
        exponent -= 1;
    }
    var remainder = numerator - denominator;
    var significand = 1u;
    for (var i = 0u; i < 23u; i++) {
        remainder *= 2u;
        significand <<= 1u;
        if (remainder >= denominator) { significand |= 1u; remainder -= denominator; }
    }
    let doubled = remainder * 2u;
    if (doubled > denominator || (doubled == denominator && (significand & 1u) != 0u)) {
        significand += 1u;
    }
    let encoded = (u32(exponent + 127) << 23u) + significand - 8388608u;
    return bitcast<f32>(encoded | select(0u, 2147483648u, value < 0));
}
fn expanded_endpoint(value: i32, signed: bool) -> i32 {
    if (!signed) { return value * 257; }
    let magnitude = (abs(value) * 16513) >> 6u;
    return select(magnitude, -magnitude, value < 0);
}
fn channel(base: u32, texel: u32, signed: bool) -> f32 {
    let lo = blocks[base];
    let hi = blocks[base + 1u];
    var a = i32(lo & 255u);
    var b = i32((lo >> 8u) & 255u);
    if (signed) {
        a = max((a << 24u) >> 24u, -127);
        b = max((b << 24u) >> 24u, -127);
    }
    let index = bits(lo, hi, 16u + texel * 3u, 3u);
    let divisor = select(65535u, 32767u, signed);
    if (parameters.profile == 3u) {
        if (a <= b && index >= 6u) {
            return select(select(0.0, -1.0, signed), 1.0, index == 7u);
        }
        var weight = 0;
        if (index == 1u) { weight = 64; }
        if (index >= 2u) {
            let denominator = select(5u, 7u, a > b);
            weight = i32(((index - 1u) * 64u + denominator / 2u) / denominator);
        }
        let interpolated = (64 - weight) * a + weight * b;
        let result = normalized(interpolated, select(16320u, 8128u, signed));
        if (signed) { return result; }
        /** @brief Measured AMD UNorm14 conversion rounds these integers one ULP below nearest.
         * @note Includes unreachable interpolator inputs; keeping the complete conversion set
         * avoids depending on endpoint/index reachability when the decoder is maintained.
         */
        switch interpolated {
            case 8, 9, 10, 11, 12, 13, 14, 15, 767, 1279, 1534, 1789,
                 2303, 2558, 2813, 3068, 3323, 3578, 3833: {
                return bitcast<f32>(bitcast<u32>(result) - 1u);
            }
            default: { return result; }
        }
    }
    if (parameters.profile == 2u) {
        if (a <= b && index >= 6u) {
            return select(select(0.0, -1.0, signed), 1.0, index == 7u);
        }
        var weight = 0;
        if (index == 1u) { weight = 65536; }
        if (index >= 2u) {
            let denominator = select(5u, 7u, a > b);
            weight = i32(((index - 1u) * 65536u + denominator / 2u) / denominator);
        }
        let interpolated = ((65536 - weight) * a + weight * b + 128) >> 8u;
        if (!signed) {
            return normalized(interpolated + (interpolated >> 8u), divisor);
        }
        let magnitude = abs(interpolated);
        let expanded = magnitude + (magnitude >> 7u) + (magnitude >> 14u);
        return normalized(select(expanded, -expanded, interpolated < 0), divisor);
    }
    if (index == 0u) { return normalized(expanded_endpoint(a, signed), divisor); }
    if (index == 1u) { return normalized(expanded_endpoint(b, signed), divisor); }
    if (a <= b && index >= 6u) {
        return select(select(0.0, -1.0, signed), 1.0, index == 7u);
    }
    var anchor = a;
    var delta = b - a;
    var multiple = 1;
    if (a > b) {
        if (index == 3u || index == 6u) { multiple = 2; }
        if (index == 4u || index == 5u) { multiple = 4; }
        if (index == 4u || index >= 6u) { anchor = b; delta = -delta; }
    } else {
        if (index == 3u || index == 4u) { multiple = 2; }
        if (index >= 4u) { anchor = b; delta = -delta; }
    }
    let expanded = expanded_endpoint(anchor, signed);
    let result = expanded + select(48, 36, a > b) * delta * multiple;
    return normalized(result, select(65535u, 32767u, signed));
}
fn rgb565(value: u32) -> vec3u {
    let r = (value >> 11u) & 31u;
    let g = (value >> 5u) & 63u;
    let b = value & 31u;
    return vec3u((r << 3u) | (r >> 2u), (g << 2u) | (g >> 4u), (b << 3u) | (b >> 2u));
}
@vertex fn vs(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    var positions = array<vec2f, 3>(vec2f(-1,-1), vec2f(3,-1), vec2f(-1,3));
    return vec4f(positions[index], 0, 1);
}
@fragment fn fs(@builtin(position) position: vec4f) -> @location(0) vec4f {
    let pixel = vec2u(position.xy);
    let texel = (pixel.y % 4u) * 4u + pixel.x % 4u;
    let words = select(4u, 2u, parameters.kind == 1u || parameters.kind == 4u || parameters.kind == 5u);
    let base = (pixel.y / 4u) * parameters.row_words + (pixel.x / 4u) * words;
    if (parameters.kind == 8u || parameters.kind == 9u) { return decode_bc6(base, texel, parameters.kind == 9u); }
    if (parameters.kind == 10u) { return decode_bc7(base, texel); }
    if (parameters.kind >= 4u && parameters.kind <= 7u) {
        let signed = parameters.kind == 5u || parameters.kind == 7u;
        var green = 0.0;
        if (parameters.kind >= 6u) { green = channel(base + 2u, texel, signed); }
        return vec4f(channel(base, texel, signed), green, 0.0, 1.0);
    }
    let color_base = base + select(2u, 0u, parameters.kind == 1u);
    let packed = blocks[color_base];
    let endpoint0 = packed & 65535u;
    let endpoint1 = packed >> 16u;
    let c0 = rgb565(endpoint0);
    let c1 = rgb565(endpoint1);
    let index = (blocks[color_base + 1u] >> (texel * 2u)) & 3u;
    var color = c0;
    var alpha = 255u;
    if (index == 1u) { color = c1; }
    if (index >= 2u) {
        if (parameters.kind != 1u || endpoint0 > endpoint1) {
            if (index == 2u) { color = (2u * c0 + c1) / 3u; }
            else { color = (c0 + 2u * c1) / 3u; }
        } else {
            if (index == 2u) { color = (c0 + c1) / 2u; }
            else { color = vec3u(0u); alpha = 0u; }
        }
        if (parameters.profile == 1u && alpha != 0u) {
            let rb0 = vec2u((endpoint0 >> 11u) & 31u, endpoint0 & 31u);
            let rb1 = vec2u((endpoint1 >> 11u) & 31u, endpoint1 & 31u);
            let g0 = i32(c0.y);
            let g1 = i32(c1.y);
            let delta = g1 - g0;
            if (parameters.kind != 1u || endpoint0 > endpoint1) {
                let rb = select((rb0 + 2u * rb1) * 22u, (2u * rb0 + rb1) * 22u, index == 2u) >> vec2u(3u);
                let scaled = 80 * delta + (delta >> 2u);
                var green = select(g1 + ((128 - scaled) >> 8u), g0 + ((128 + scaled) >> 8u), index == 2u);
                if (endpoint0 < endpoint1) {
                    let reversed = -80 * delta + ((-delta) >> 2u);
                    green = select(g1 + ((128 + reversed) >> 8u), g0 + ((128 - reversed) >> 8u), index == 2u);
                }
                color = vec3u(rb.x, u32(green), rb.y);
            } else {
                let rb = ((rb0 + rb1) * 33u) >> vec2u(3u);
                let green = g0 + ((128 + 128 * delta + (delta >> 2u)) >> 8u);
                color = vec3u(rb.x, u32(green), rb.y);
            }
        }
        if ((parameters.profile == 2u || parameters.profile == 3u) && alpha != 0u) {
            let unit = select(256u, 64u, parameters.profile == 3u);
            var weight = unit / 2u;
            if (parameters.kind != 1u || endpoint0 > endpoint1) {
                weight = select((2u * unit + 1u) / 3u, (unit + 1u) / 3u, index == 2u);
            }
            color = ((unit - weight) * c0 + weight * c1 + unit / 2u) / unit;
        }
    }
    if (parameters.kind == 2u) {
        alpha = bits(blocks[base], blocks[base + 1u], texel * 4u, 4u) * 17u;
    }
    if (parameters.kind == 3u) {
        let a0 = blocks[base] & 255u;
        let a1 = (blocks[base] >> 8u) & 255u;
        let ai = bits(blocks[base], blocks[base + 1u], 16u + texel * 3u, 3u);
        if (ai == 0u) { alpha = a0; }
        else if (ai == 1u) { alpha = a1; }
        else if (a0 > a1) { alpha = ((8u - ai) * a0 + (ai - 1u) * a1 + 3u) / 7u; }
        else if (ai < 6u) { alpha = ((6u - ai) * a0 + (ai - 1u) * a1 + 2u) / 5u; }
        else { alpha = select(255u, 0u, ai == 6u); }
        if (parameters.profile == 1u && ai >= 2u && (a0 > a1 || ai < 6u)) {
            var anchor = i32(a0);
            var delta = i32(a1) - i32(a0);
            var multiple = 1;
            if (a0 > a1) {
                if (ai == 3u || ai == 6u) { multiple = 2; }
                if (ai == 4u || ai == 5u) { multiple = 4; }
                if (ai == 4u || ai >= 6u) { anchor = i32(a1); delta = -delta; }
            } else {
                if (ai == 3u || ai == 4u) { multiple = 2; }
                if (ai >= 4u) { anchor = i32(a1); delta = -delta; }
            }
            let product = delta * multiple;
            let scale = select(48, 36, a0 > a1);
            alpha = u32(anchor + ((scale * product + (product >> 3u) + 128) >> 8u));
        }
        if ((parameters.profile == 2u || parameters.profile == 3u) && ai >= 2u && (a0 > a1 || ai < 6u)) {
            let unit = select(256u, 64u, parameters.profile == 3u);
            let denominator = select(5u, 7u, a0 > a1);
            let weight = ((ai - 1u) * unit + denominator / 2u) / denominator;
            alpha = ((unit - weight) * a0 + weight * a1 + unit / 2u) / unit;
        }
    }
    return vec4f(vec3f(color), f32(alpha)) / 255.0;
}
)";
		static std::mutex decoders_mutex;
		static std::vector<std::weak_ptr<BCDecoder>> decoders;
		auto FindDecoder = [&]() -> std::shared_ptr<BCDecoder> {
			std::erase_if(decoders, [](auto const& weak) {
				return weak.expired();
			});
			for (auto const& weak : decoders) {
				auto candidate = weak.lock();
				if (candidate && candidate->device.Get() == device.Get() &&
				    candidate->format == texture.GetFormat() &&
				    static_cast<bool>(candidate->sampler) != software) {
					return candidate;
				}
			}
			return {};
		};
		std::shared_ptr<BCDecoder> cached;
		{
			std::unique_lock lock(decoders_mutex);
			cached = FindDecoder();
		}
		if (cached) {
			return {device.CreateBuffer(&buffer), format, std::move(cached)};
		}
		/// @brief One transient shader string joins the format-independent body and BPTC functions.
		std::string software_source;
		if (software) {
			software_source = software_code;
			software_source.append(BC67Shader());
		}
		wgpu::ShaderSourceWGSL source(
		    wgpu::ShaderSourceWGSL::Init{nullptr, software ? software_source.c_str() : code}
		);
		wgpu::ShaderModuleDescriptor shader{.nextInChain = &source};
		auto module = device.CreateShaderModule(&shader);
		wgpu::ColorTargetState target{
		    .format = software && texture.GetFormat() == wgpu::TextureFormat::RGBA8UnormSrgb ?
		        wgpu::TextureFormat::RGBA8Unorm :
		        texture.GetFormat()
		};
		wgpu::FragmentState
		    fragment{.module = module, .entryPoint = "fs", .targetCount = 1u, .targets = &target};
		wgpu::RenderPipelineDescriptor pipeline{
		    .vertex = {.module = module, .entryPoint = "vs"},
		    .fragment = &fragment
		};
		wgpu::SamplerDescriptor sampler{};
		auto decoder = std::make_shared<BCDecoder>(
		    device,
		    texture.GetFormat(),
		    device.CreateRenderPipeline(&pipeline),
		    software ? wgpu::Sampler{} : device.CreateSampler(&sampler)
		);
		{
			/** @brief Weak registry entries cannot extend device or texture lifetime. */
			std::unique_lock lock(decoders_mutex);
			if (auto existing = FindDecoder()) {
				decoder = std::move(existing);
			} else {
				decoders.emplace_back(decoder);
			}
		}
		return {device.CreateBuffer(&buffer), format, std::move(decoder)};
	}

	/** @brief Decodes retained blocks into the original-sized sampled texture.
	 * @note The native path uses a compressed scratch image. The opt-in software
	 * experiment reads storage bytes directly; neither path performs CPU readback.
	 */
	void DecodeBC(
	    wgpu::Device const& device,
	    wgpu::CommandEncoder const& encoder,
	    Resource const& resource,
	    std::uint32_t mip
	) {
		auto const& texture = std::get<wgpu::Texture>(resource.impl);
		auto layout = BCMipLayout(texture, BCBlockBytes(resource.bc.format), mip);
		if (resource.bc.decoder && !resource.bc.decoder->sampler) {
			/** @brief Each slice binds its own original rows; mip and layer offsets
			 * stay on the CPU while texel decoding stays inside this command encoder.
			 */
			wgpu::BufferDescriptor parameter_desc{
			    .usage = wgpu::BufferUsage::Uniform,
			    .size = 16u,
			    .mappedAtCreation = true
			};
			auto parameters = device.CreateBuffer(&parameter_desc);
			auto values = static_cast<std::uint32_t*>(parameters.GetMappedRange());
			if (!values) {
				throw std::runtime_error("Failed to map BC decoder parameters");
			}
			values[0] = layout.pitch / 4u;
			values[1] = SoftwareBCKind(resource.bc.format);
			/** @brief Hardware-compatible arithmetic is a transient decode parameter,
			 * not additional persistent resource or device ownership state.
			 * @note BC1–BC5 profiles follow the measured integer expressions in
			 * https://fgiesen.wordpress.com/2021/10/04/gpu-bcn-decoding/ . AMD BC1–BC5
			 * branches are implemented but still need native hardware verification.
			 */
			wgpu::AdapterInfo adapter_info;
			values[2] = 0u;
			if (device.GetAdapterInfo(&adapter_info)) {
				switch (adapter_info.vendorID) {
					case 0x10deu:
						values[2] = 1u;
						break;
					case 0x8086u:
						values[2] = 2u;
						break;
					case 0x1002u:
						values[2] = 3u;
						break;
					default:
						break;
				}
			}
			values[3] = 0u;
			parameters.Unmap();
			for (std::uint32_t layer = 0u; layer < texture.GetDepthOrArrayLayers(); ++layer) {
				bool srgb = texture.GetFormat() == wgpu::TextureFormat::RGBA8UnormSrgb;
				wgpu::Texture linear;
				if (srgb) {
					/// @brief Linear/sRGB copy-compatible textures preserve stored bytes;
					/// compatibility mode does not allow a linear view of an sRGB image.
					wgpu::TextureDescriptor linear_desc{
					    .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc,
					    .size = {layout.width, layout.height, 1u},
					    .format = wgpu::TextureFormat::RGBA8Unorm
					};
					linear = device.CreateTexture(&linear_desc);
				}
				wgpu::TextureViewDescriptor view_desc{
				    .format = srgb ? wgpu::TextureFormat::RGBA8Unorm : texture.GetFormat(),
				    .dimension = wgpu::TextureViewDimension::e2D,
				    .baseMipLevel = srgb ? 0u : mip,
				    .mipLevelCount = 1u,
				    .baseArrayLayer = srgb ? 0u : layer,
				    .arrayLayerCount = 1u
				};
				auto output = (srgb ? linear : texture).CreateView(&view_desc);
				wgpu::BindGroupEntry entries[]{
				    {.binding = 0u,
					 .buffer = resource.bc.blocks,
					 .offset = layout.offset +
					     static_cast<std::uint64_t>(layer) * layout.rows * layout.pitch,
					 .size = static_cast<std::uint64_t>(layout.rows) * layout.pitch},
				    {.binding = 1u, .buffer = parameters, .size = 16u}
				};
				wgpu::BindGroupDescriptor group_desc{
				    .layout = resource.bc.decoder->impl.GetBindGroupLayout(0u),
				    .entryCount = 2u,
				    .entries = entries
				};
				auto group = device.CreateBindGroup(&group_desc);
				wgpu::RenderPassColorAttachment color{
				    .view = output,
				    .loadOp = wgpu::LoadOp::Clear,
				    .storeOp = wgpu::StoreOp::Store
				};
				wgpu::RenderPassDescriptor pass_desc{
				    .colorAttachmentCount = 1u,
				    .colorAttachments = &color
				};
				auto pass = encoder.BeginRenderPass(&pass_desc);
				pass.SetPipeline(resource.bc.decoder->impl);
				pass.SetBindGroup(0u, group);
				pass.Draw(3u);
				pass.End();
				if (srgb) {
					wgpu::TexelCopyTextureInfo source{.texture = linear};
					wgpu::TexelCopyTextureInfo destination{
					    .texture = texture,
					    .mipLevel = mip,
					    .origin = {0u, 0u, layer}
					};
					wgpu::Extent3D extent{layout.width, layout.height, 1u};
					encoder.CopyTextureToTexture(&source, &destination, &extent);
				}
			}
			return;
		}
		wgpu::TextureDescriptor scratch_desc{
		    .usage = wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::TextureBinding,
		    .size = {(layout.width + 3u) / 4u * 4u, (layout.height + 3u) / 4u * 4u, 1u},
		    .format = resource.bc.format
		};
		wgpu::TexelCopyBufferInfo source{
		    .layout =
		        {.offset = layout.offset, .bytesPerRow = layout.pitch, .rowsPerImage = layout.rows},
		    .buffer = resource.bc.blocks
		};
		wgpu::Extent3D extent{scratch_desc.size};
		if (!resource.bc.decoder) {
			/// @brief Native BC textures retain hardware sampling. Copy extents of
			/// small mips describe physical blocks, not their smaller virtual size.
			wgpu::TexelCopyTextureInfo destination{.texture = texture, .mipLevel = mip};
			extent.depthOrArrayLayers = texture.GetDepthOrArrayLayers();
			encoder.CopyBufferToTexture(&source, &destination, &extent);
			return;
		}
		for (std::uint32_t layer = 0u; layer < texture.GetDepthOrArrayLayers(); ++layer) {
			/// @note Compatibility mode fixes the sampled view dimension at texture
			/// creation. One scratch per slice avoids sampling a 2D view of an array.
			auto scratch = device.CreateTexture(&scratch_desc);
			wgpu::TexelCopyTextureInfo destination{.texture = scratch};
			source.layout.offset =
			    layout.offset + static_cast<std::uint64_t>(layer) * layout.rows * layout.pitch;
			encoder.CopyBufferToTexture(&source, &destination, &extent);
			wgpu::TextureViewDescriptor input_desc{
			    .dimension = wgpu::TextureViewDimension::e2D,
			    .mipLevelCount = 1u,
			    .arrayLayerCount = 1u
			};
			auto input = scratch.CreateView(&input_desc);
			input_desc.baseMipLevel = mip;
			input_desc.baseArrayLayer = layer;
			auto output = texture.CreateView(&input_desc);
			wgpu::BindGroupEntry entries[]{
			    {.binding = 0u, .textureView = input},
			    {.binding = 1u, .sampler = resource.bc.decoder->sampler}
			};
			wgpu::BindGroupDescriptor group_desc{
			    .layout = resource.bc.decoder->impl.GetBindGroupLayout(0u),
			    .entryCount = 2u,
			    .entries = entries
			};
			auto group = device.CreateBindGroup(&group_desc);
			wgpu::RenderPassColorAttachment color{
			    .view = output,
			    .loadOp = wgpu::LoadOp::Clear,
			    .storeOp = wgpu::StoreOp::Store
			};
			wgpu::RenderPassDescriptor pass_desc{
			    .colorAttachmentCount = 1u,
			    .colorAttachments = &color
			};
			auto pass = encoder.BeginRenderPass(&pass_desc);
			pass.SetPipeline(resource.bc.decoder->impl);
			pass.SetBindGroup(0u, group);
			pass.Draw(3u);
			pass.End();
		}
	}

	/** @brief Copies BC rows losslessly between external layouts and private storage. */
	void CopyBCBuffer(
	    wgpu::CommandEncoder const& encoder,
	    Resource const& texture_resource,
	    wgpu::Buffer const& buffer,
	    TextureDataLayout const& external,
	    TextureRegion const& region,
	    bool upload
	) {
		auto const& texture = std::get<wgpu::Texture>(texture_resource.impl);
		auto bytes = BCBlockBytes(texture_resource.bc.format);
		if (region.mip_level >= texture.GetMipLevelCount()) {
			throw std::out_of_range("BC mip exceeds texture");
		}
		auto layout = BCMipLayout(texture, bytes, region.mip_level);
		if (region.width == 0u || region.height == 0u || region.array_layer_count == 0u ||
		    region.offset_x % 4u != 0u || region.offset_y % 4u != 0u || region.offset_z != 0u ||
		    region.depth != 1u || external.offset % bytes != 0u ||
		    region.base_array_layer >= texture.GetDepthOrArrayLayers() ||
		    region.array_layer_count > texture.GetDepthOrArrayLayers() - region.base_array_layer ||
		    region.offset_x >= layout.width || region.offset_y >= layout.height ||
		    (region.width % 4u != 0u && region.offset_x + region.width != layout.width) ||
		    (region.height % 4u != 0u && region.offset_y + region.height != layout.height) ||
		    region.width > (layout.width + 3u) / 4u * 4u - region.offset_x ||
		    region.height > (layout.height + 3u) / 4u * 4u - region.offset_y) {
			throw std::invalid_argument("Invalid BC copy region or alignment");
		}
		auto row_bytes = static_cast<std::uint64_t>((region.width + 3u) / 4u) * bytes;
		auto rows = (region.height + 3u) / 4u;
		auto pitch = external.bytes_per_row != 0u ? external.bytes_per_row : row_bytes;
		auto image_rows = external.rows_per_image != 0u ? external.rows_per_image : rows;
		if (pitch < row_bytes || pitch % bytes != 0u || image_rows < rows) {
			throw std::invalid_argument("Invalid BC buffer pitch");
		}
		for (std::uint32_t layer = 0u; layer < region.array_layer_count; ++layer) {
			for (std::uint32_t row = 0u; row < rows; ++row) {
				auto offset = external.offset +
				    (static_cast<std::uint64_t>(layer) * image_rows + row) * pitch;
				if (offset > buffer.GetSize() || row_bytes > buffer.GetSize() - offset) {
					throw std::out_of_range("BC copy exceeds buffer");
				}
				auto native_offset = layout.offset +
				    (static_cast<std::uint64_t>(region.base_array_layer + layer) * layout.rows +
					 region.offset_y / 4u + row) *
				        layout.pitch +
				    region.offset_x / 4u * bytes;
				if (upload) {
					encoder.CopyBufferToBuffer(
					    buffer,
					    offset,
					    texture_resource.bc.blocks,
					    native_offset,
					    row_bytes
					);
				} else {
					encoder.CopyBufferToBuffer(
					    texture_resource.bc.blocks,
					    native_offset,
					    buffer,
					    offset,
					    row_bytes
					);
				}
			}
		}
	}
} // namespace fyuu_rhi::webgpu
