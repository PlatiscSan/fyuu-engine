module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string_view>
#endif
module fyuu_rhi:webgpu_bc67;
#if defined(__cpp_lib_modules)
import std;
#endif

namespace fyuu_rhi::webgpu {
	/**
     * @brief Supplies complete BC6H/BC7 GPU decoding, including partitions and fix-up indices.
     * @note Bit layouts and partition data follow the BPTC specification, cross-checked
     * against https://github.com/iOrange/bcdec . The decoder keeps all endpoint and
     * interpolation operations integer until conversion to the specified output format.
     * BC6H reserved modes produce opaque black; a missing BC7 mode produces transparent black.
     * The partition table contains two sets of 64 shapes. Each vec2u packs sixteen
     * four-bit entries: low two bits select a subset, bit two marks its fix-up texel.
     * The BC6H field table contains 32 words per mode. Each byte maps one source bit
     * to (component * 4 + endpoint) * 16 + endpoint_bit; 192..196 select partition
     * bits and 255 marks unused mode bits. These are format data, not executable code.
     */
	std::string_view BC67Shader() noexcept {
		static constexpr char source[] = R"WGSL(
const partitions = array<vec2u, 128>(
    vec2u(0x11001104u, 0x51001100u),
    vec2u(0x10001004u, 0x50001000u),
    vec2u(0x11101114u, 0x51101110u),
    vec2u(0x11001004u, 0x51101100u),
    vec2u(0x10000004u, 0x51001000u),
    vec2u(0x11101104u, 0x51111110u),
    vec2u(0x11001004u, 0x51111110u),
    vec2u(0x10000004u, 0x51101100u),
    vec2u(0x4u, 0x51001000u),
    vec2u(0x11101104u, 0x51111111u),
    vec2u(0x10000004u, 0x51111110u),
    vec2u(0x4u, 0x51101000u),
    vec2u(0x11101004u, 0x51111111u),
    vec2u(0x4u, 0x51111111u),
    vec2u(0x11110004u, 0x51111111u),
    vec2u(0x4u, 0x51110000u),
    vec2u(0x10004u, 0x51110111u),
    vec2u(0x10001514u, 0x0u),
    vec2u(0x4u, 0x1110005u),
    vec2u(0x11001514u, 0x1000u),
    vec2u(0x10001504u, 0x0u),
    vec2u(0x10004u, 0x1110015u),
    vec2u(0x4u, 0x110005u),
    vec2u(0x11001114u, 0x50001100u),
    vec2u(0x10001504u, 0x1000u),
    vec2u(0x10004u, 0x110005u),
    vec2u(0x1100514u, 0x1100110u),
    vec2u(0x1101504u, 0x110110u),
    vec2u(0x11101004u, 0x10115u),
    vec2u(0x11110004u, 0x1115u),
    vec2u(0x10001514u, 0x1110001u),
    vec2u(0x10011504u, 0x111001u),
    vec2u(0x10101014u, 0x50101010u),
    vec2u(0x11110004u, 0x51110000u),
    vec2u(0x5011014u, 0x1011010u),
    vec2u(0x11001104u, 0x110015u),
    vec2u(0x111504u, 0x111100u),
    vec2u(0x10101014u, 0x1010105u),
    vec2u(0x10010114u, 0x50010110u),
    vec2u(0x1011014u, 0x50100101u),
    vec2u(0x11001514u, 0x1110011u),
    vec2u(0x11001004u, 0x10015u),
    vec2u(0x1001504u, 0x110010u),
    vec2u(0x11011504u, 0x111011u),
    vec2u(0x10010514u, 0x1101001u),
    vec2u(0x111104u, 0x51000011u),
    vec2u(0x1100114u, 0x50011001u),
    vec2u(0x5100004u, 0x110u),
    vec2u(0x5110014u, 0x10u),
    vec2u(0x11100504u, 0x100u),
    vec2u(0x5000004u, 0x1001110u),
    vec2u(0x100004u, 0x100115u),
    vec2u(0x110114u, 0x51001001u),
    vec2u(0x1101104u, 0x50010011u),
    vec2u(0x11000514u, 0x111001u),
    vec2u(0x10011504u, 0x1100011u),
    vec2u(0x110114u, 0x50010011u),
    vec2u(0x11000114u, 0x50011100u),
    vec2u(0x1111114u, 0x50000001u),
    vec2u(0x11004u, 0x51100111u),
    vec2u(0x11110004u, 0x51001100u),
    vec2u(0x11001504u, 0x1111u),
    vec2u(0x1000504u, 0x1110111u),
    vec2u(0x100014u, 0x51101110u),
    vec2u(0x11005104u, 0x62221220u),
    vec2u(0x11005004u, 0x12221126u),
    vec2u(0x10020004u, 0x51221126u),
    vec2u(0x22006224u, 0x51101100u),
    vec2u(0x4u, 0x62112215u),
    vec2u(0x11005104u, 0x62002200u),
    vec2u(0x22006204u, 0x51111111u),
    vec2u(0x11001104u, 0x51221126u),
    vec2u(0x4u, 0x62221115u),
    vec2u(0x11110004u, 0x62221115u),
    vec2u(0x15110004u, 0x62222222u),
    vec2u(0x25002104u, 0x61002100u),
    vec2u(0x25102114u, 0x61102110u),
    vec2u(0x22502214u, 0x62102210u),
    vec2u(0x21105104u, 0x62212211u),
    vec2u(0x10025104u, 0x2220026u),
    vec2u(0x11005004u, 0x62112110u),
    vec2u(0x11005114u, 0x221006u),
    vec2u(0x22110004u, 0x62112215u),
    vec2u(0x22006204u, 0x51112200u),
    vec2u(0x11105114u, 0x62202220u),
    vec2u(0x10005004u, 0x12221226u),
    vec2u(0x15000004u, 0x62102210u),
    vec2u(0x110004u, 0x1220526u),
    vec2u(0x22506214u, 0x1100u),
    vec2u(0x21002104u, 0x62222215u),
    vec2u(0x16210114u, 0x1101225u),
    vec2u(0x5100004u, 0x12211621u),
    vec2u(0x20112204u, 0x62002015u),
    vec2u(0x1500114u, 0x62222002u),
    vec2u(0x22101104u, 0x51002610u),
    vec2u(0x20004u, 0x52221126u),
    vec2u(0x20000004u, 0x62212215u),
    vec2u(0x22006224u, 0x51002100u),
    vec2u(0x21005104u, 0x62202200u),
    vec2u(0x2500214u, 0x2100610u),
    vec2u(0x15110004u, 0x2622u),
    vec2u(0x10210214u, 0x2102506u),
    vec2u(0x21020214u, 0x2101065u),
    vec2u(0x221104u, 0x51002611u),
    vec2u(0x26111104u, 0x51000022u),
    vec2u(0x10105014u, 0x62222222u),
    vec2u(0x4u, 0x52121216u),
    vec2u(0x22512204u, 0x62112200u),
    vec2u(0x11006204u, 0x51002200u),
    vec2u(0x16210224u, 0x52210220u),
    vec2u(0x26221014u, 0x50102222u),
    vec2u(0x12120004u, 0x52121216u),
    vec2u(0x10105014u, 0x62221010u),
    vec2u(0x11106224u, 0x51102220u),
    vec2u(0x21512004u, 0x61112000u),
    vec2u(0x21520004u, 0x61122112u),
    vec2u(0x11502224u, 0x62201110u),
    vec2u(0x21112004u, 0x60002115u),
    vec2u(0x1500114u, 0x62220110u),
    vec2u(0x4u, 0x61122512u),
    vec2u(0x1500114u, 0x62222222u),
    vec2u(0x11002204u, 0x62001500u),
    vec2u(0x22112204u, 0x62002215u),
    vec2u(0x4u, 0x61520000u),
    vec2u(0x10006004u, 0x50002000u),
    vec2u(0x22212224u, 0x62252220u),
    vec2u(0x22225014u, 0x62222222u),
    vec2u(0x11025114u, 0x2221026u)
);
const bc6_fields = array<u32, 448>(
    0xa464ffffu, 0x20100b4u, 0x6050403u, 0x40090807u, 0x44434241u, 0x48474645u, 0x82818049u, 0x86858483u,
    0x10898887u, 0x14131211u, 0x62616074u, 0x52515063u, 0x70b05453u, 0x90737271u, 0x94939291u, 0xa2a1a0b1u,
    0x222120a3u, 0x30b22423u, 0x34333231u, 0xc2c1c0b3u, 0xffffc4c3u, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0x7465ffffu, 0x2010075u, 0x6050403u, 0x40a4b1b0u, 0x44434241u, 0xb2a54645u, 0x82818064u, 0x86858483u,
    0x10b4b5b3u, 0x14131211u, 0x62616015u, 0x52515063u, 0x70555453u, 0x90737271u, 0x94939291u, 0xa2a1a095u,
    0x222120a3u, 0x30252423u, 0x34333231u, 0xc2c1c035u, 0xffffc4c3u, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0x20100ffu, 0x6050403u, 0x40090807u, 0x44434241u, 0x48474645u, 0x82818049u, 0x86858483u,
    0x10898887u, 0x14131211u, 0x6261600au, 0x52515063u, 0x70b04a53u, 0x90737271u, 0x8a939291u, 0xa2a1a0b1u,
    0x222120a3u, 0x30b22423u, 0x34333231u, 0xc2c1c0b3u, 0xffffc4c3u, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0x20100ffu, 0x6050403u, 0x40090807u, 0x44434241u, 0x48474645u, 0x82818049u, 0x86858483u,
    0x10898887u, 0xa131211u, 0x62616074u, 0x52515063u, 0x704a5453u, 0x90737271u, 0x8a939291u, 0xa2a1a0b1u,
    0x222120a3u, 0x30b2b023u, 0x64333231u, 0xc2c1c0b3u, 0xffffc4c3u, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0x20100ffu, 0x6050403u, 0x40090807u, 0x44434241u, 0x48474645u, 0x82818049u, 0x86858483u,
    0x10898887u, 0xa131211u, 0x626160a4u, 0x52515063u, 0x70b04a53u, 0x90737271u, 0x94939291u, 0xa2a1a08au,
    0x222120a3u, 0x30b2b123u, 0xb4333231u, 0xc2c1c0b3u, 0xffffc4c3u, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0x20100ffu, 0x6050403u, 0x40a40807u, 0x44434241u, 0x48474645u, 0x82818064u, 0x86858483u,
    0x10b48887u, 0x14131211u, 0x62616074u, 0x52515063u, 0x70b05453u, 0x90737271u, 0x94939291u, 0xa2a1a0b1u,
    0x222120a3u, 0x30b22423u, 0x34333231u, 0xc2c1c0b3u, 0xffffc4c3u, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0x20100ffu, 0x6050403u, 0x40a47407u, 0x44434241u, 0xb2474645u, 0x82818064u, 0x86858483u,
    0x10b4b387u, 0x14131211u, 0x62616015u, 0x52515063u, 0x70b05453u, 0x90737271u, 0x94939291u, 0xa2a1a0b1u,
    0x222120a3u, 0x30252423u, 0x34333231u, 0xc2c1c035u, 0xffffc4c3u, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0x20100ffu, 0x6050403u, 0x40a4b007u, 0x44434241u, 0x65474645u, 0x82818064u, 0x86858483u,
    0x10b47587u, 0x14131211u, 0x62616074u, 0x52515063u, 0x70555453u, 0x90737271u, 0x94939291u, 0xa2a1a0b1u,
    0x222120a3u, 0x30b22423u, 0x34333231u, 0xc2c1c0b3u, 0xffffc4c3u, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0x20100ffu, 0x6050403u, 0x40a4b107u, 0x44434241u, 0xa5474645u, 0x82818064u, 0x86858483u,
    0x10b4b587u, 0x14131211u, 0x62616074u, 0x52515063u, 0x70b05453u, 0x90737271u, 0x94939291u, 0xa2a1a095u,
    0x222120a3u, 0x30b22423u, 0x34333231u, 0xc2c1c0b3u, 0xffffc4c3u, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0x20100ffu, 0x74050403u, 0x40a4b1b0u, 0x44434241u, 0xb2a56545u, 0x82818064u, 0x75858483u,
    0x10b4b5b3u, 0x14131211u, 0x62616015u, 0x52515063u, 0x70555453u, 0x90737271u, 0x94939291u, 0xa2a1a095u,
    0x222120a3u, 0x30252423u, 0x34333231u, 0xc2c1c035u, 0xffffc4c3u, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0x20100ffu, 0x6050403u, 0x40090807u, 0x44434241u, 0x48474645u, 0x82818049u, 0x86858483u,
    0x10898887u, 0x14131211u, 0x18171615u, 0x52515019u, 0x56555453u, 0x90595857u, 0x94939291u, 0x98979695u,
    0xffffff99u, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0x20100ffu, 0x6050403u, 0x40090807u, 0x44434241u, 0x48474645u, 0x82818049u, 0x86858483u,
    0x10898887u, 0x14131211u, 0x18171615u, 0x5251500au, 0x56555453u, 0x904a5857u, 0x94939291u, 0x98979695u,
    0xffffff8au, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0x20100ffu, 0x6050403u, 0x40090807u, 0x44434241u, 0x48474645u, 0x82818049u, 0x86858483u,
    0x10898887u, 0x14131211u, 0xb171615u, 0x5251500au, 0x56555453u, 0x904a4b57u, 0x94939291u, 0x8b979695u,
    0xffffff8au, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0x20100ffu, 0x6050403u, 0x40090807u, 0x44434241u, 0x48474645u, 0x82818049u, 0x86858483u,
    0x10898887u, 0xf131211u, 0xb0c0d0eu, 0x5251500au, 0x4d4e4f53u, 0x904a4b4cu, 0x8f939291u, 0x8b8c8d8eu,
    0xffffff8au, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
    0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu
);

fn read_block(base: u32, cursor: ptr<function, u32>, count: u32) -> u32 {
    if (count == 0u) { return 0u; }
    let at = *cursor;
    *cursor = at + count;
    let shift = at & 31u;
    var value = blocks[base + at / 32u] >> shift;
    if (shift + count > 32u) { value |= blocks[base + at / 32u + 1u] << (32u - shift); }
    return value & ((1u << count) - 1u);
}
fn subset(partition_id: u32, subsets: u32, texel: u32) -> u32 {
    if (subsets == 1u) { return select(0u, 4u, texel == 0u); }
    let packed = partitions[(subsets - 2u) * 64u + partition_id];
    return (packed[texel / 8u] >> ((texel & 7u) * 4u)) & 7u;
}
fn weight(bits: u32, index: u32) -> u32 {
    if (bits == 2u) { let values = array<u32, 4>(0u, 21u, 43u, 64u); return values[index]; }
    if (bits == 3u) { let values = array<u32, 8>(0u, 9u, 18u, 27u, 37u, 46u, 55u, 64u); return values[index]; }
    let values = array<u32, 16>(0u, 4u, 9u, 13u, 17u, 21u, 26u, 30u, 34u, 38u, 43u, 47u, 51u, 55u, 60u, 64u);
    return values[index];
}
fn decode_bc7(base: u32, texel: u32) -> vec4f {
    var cursor = 0u;
    var mode = 0u;
    for (; mode < 8u; mode++) { if (read_block(base, &cursor, 1u) != 0u) { break; } }
    if (mode == 8u) { return vec4f(0.0); }
    let color_bits = array<u32, 8>(4u, 6u, 5u, 7u, 5u, 7u, 7u, 5u);
    let alpha_bits = array<u32, 8>(0u, 0u, 0u, 0u, 6u, 8u, 7u, 5u);
    let has_p = (203u >> mode) & 1u;
    var groups = 1u;
    var partition_id = 0u;
    if (mode == 0u || mode == 1u || mode == 2u || mode == 3u || mode == 7u) {
        groups = select(2u, 3u, mode == 0u || mode == 2u);
        partition_id = read_block(base, &cursor, select(6u, 4u, mode == 0u));
    }
    var rotation = 0u;
    var selection = 0u;
    if (mode == 4u || mode == 5u) {
        rotation = read_block(base, &cursor, 2u);
        if (mode == 4u) { selection = read_block(base, &cursor, 1u); }
    }
    var endpoints: array<vec4u, 6>;
    for (var c = 0u; c < 4u; c++) {
        let n = select(color_bits[mode], alpha_bits[mode], c == 3u);
        for (var e = 0u; e < groups * 2u; e++) { endpoints[e][c] = read_block(base, &cursor, n); }
    }
    if (has_p != 0u) {
        var shared_p = 0u;
        for (var e = 0u; e < groups * 2u; e++) {
            var p = 0u;
            if (mode == 1u) {
                if ((e & 1u) == 0u) { shared_p = read_block(base, &cursor, 1u); }
                p = shared_p;
            } else { p = read_block(base, &cursor, 1u); }
            endpoints[e] = (endpoints[e] << vec4u(1u)) | vec4u(p);
        }
    }
    for (var e = 0u; e < groups * 2u; e++) {
        for (var c = 0u; c < 4u; c++) {
            let original = select(color_bits[mode], alpha_bits[mode], c == 3u);
            if (original == 0u) { endpoints[e][c] = 255u; }
            else {
                let n = original + has_p;
                let expanded = endpoints[e][c] << (8u - n);
                endpoints[e][c] = expanded | (expanded >> n);
            }
        }
    }
    var index_bits = 2u;
    if (mode == 0u || mode == 1u) { index_bits = 3u; }
    if (mode == 6u) { index_bits = 4u; }
    var indices: array<u32, 16>;
    for (var p = 0u; p < 16u; p++) {
        let anchor = (subset(partition_id, groups, p) & 4u) != 0u;
        indices[p] = read_block(base, &cursor, index_bits - select(0u, 1u, anchor));
    }
    var secondary_bits = 0u;
    if (mode == 4u) { secondary_bits = 3u; }
    if (mode == 5u) { secondary_bits = 2u; }
    var rgb_weight = weight(index_bits, indices[texel]);
    var alpha_weight = rgb_weight;
    if (secondary_bits != 0u) {
        var secondary = 0u;
        for (var p = 0u; p <= texel; p++) {
            secondary = read_block(base, &cursor, secondary_bits - select(0u, 1u, p == 0u));
        }
        let second_weight = weight(secondary_bits, secondary);
        if (selection == 0u) { alpha_weight = second_weight; }
        else { alpha_weight = rgb_weight; rgb_weight = second_weight; }
    }
    let pair = (subset(partition_id, groups, texel) & 3u) * 2u;
    let weights = vec4u(rgb_weight, rgb_weight, rgb_weight, alpha_weight);
    var decoded = (endpoints[pair] * (vec4u(64u) - weights) + endpoints[pair + 1u] * weights + vec4u(32u)) >> vec4u(6u);
    if (rotation != 0u) {
        let saved = decoded[rotation - 1u];
        decoded[rotation - 1u] = decoded.w;
        decoded.w = saved;
    }
    return vec4f(decoded) / 255.0;
}
fn sign_extend(value: i32, bits: u32) -> i32 { return (value << (32u - bits)) >> (32u - bits); }
fn bc6_unquantize(value: i32, bits: u32, signed: bool) -> i32 {
    if (!signed) {
        if (bits >= 15u) { return value; }
        if (value == 0) { return 0; }
        if (value == (1 << bits) - 1) { return 65535; }
        return ((value << 16u) + 32768) >> bits;
    }
    if (bits >= 16u) { return value; }
    let magnitude = abs(value);
    var result = 0;
    if (magnitude >= (1 << (bits - 1u)) - 1) { result = 32767; }
    else if (magnitude != 0) { result = ((magnitude << 15u) + 16384) >> (bits - 1u); }
    return select(result, -result, value < 0);
}
fn bc6_half(value: i32, signed: bool) -> f32 {
    var encoded = u32((value * 31) >> 6u);
    if (signed) {
        encoded = u32((abs(value) * 31) >> 5u);
        if (value < 0 && encoded != 0u) { encoded |= 32768u; }
    }
    return unpack2x16float(encoded).x;
}
fn decode_bc6(base: u32, texel: u32, signed: bool) -> vec4f {
    var cursor = 0u;
    var raw = read_block(base, &cursor, 2u);
    if (raw > 1u) { raw |= read_block(base, &cursor, 3u) << 2u; }
    let raw_modes = array<u32, 14>(0u, 1u, 2u, 6u, 10u, 14u, 18u, 22u, 26u, 30u, 3u, 7u, 11u, 15u);
    var mode = 14u;
    for (var m = 0u; m < 14u; m++) { if (raw_modes[m] == raw) { mode = m; break; } }
    if (mode == 14u) { return vec4f(0.0, 0.0, 0.0, 1.0); }
    let widths = array<vec4u, 14>(
        vec4u(10,5,5,5), vec4u(7,6,6,6), vec4u(11,5,4,4), vec4u(11,4,5,4),
        vec4u(11,4,4,5), vec4u(9,5,5,5), vec4u(8,6,5,5), vec4u(8,5,6,5),
        vec4u(8,5,5,6), vec4u(6,6,6,6), vec4u(10,10,10,10), vec4u(11,9,9,9),
        vec4u(12,8,8,8), vec4u(16,4,4,4));
    var endpoints: array<i32, 12>;
    var partition_id = 0u;
    let index_start = select(82u, 65u, mode >= 10u);
    for (var at = cursor; at < index_start; at++) {
        let packed = bc6_fields[mode * 32u + at / 4u];
        let destination = (packed >> ((at & 3u) * 8u)) & 255u;
        let bit = (blocks[base + at / 32u] >> (at & 31u)) & 1u;
        if (destination < 192u) { endpoints[destination / 16u] |= i32(bit << (destination & 15u)); }
        else if (destination < 197u) { partition_id |= bit << (destination - 192u); }
    }
    let count = select(4u, 2u, mode >= 10u);
    let n = widths[mode].x;
    for (var c = 0u; c < 3u; c++) {
        let first = c * 4u;
        if (signed) { endpoints[first] = sign_extend(endpoints[first], n); }
        for (var e = 1u; e < count; e++) {
            if ((mode != 9u && mode != 10u) || signed) {
                endpoints[first + e] = sign_extend(endpoints[first + e], widths[mode][c + 1u]);
            }
            if (mode != 9u && mode != 10u) {
                endpoints[first + e] = (endpoints[first + e] + endpoints[first]) & ((1 << n) - 1);
                if (signed) { endpoints[first + e] = sign_extend(endpoints[first + e], n); }
            }
        }
        for (var e = 0u; e < count; e++) { endpoints[first + e] = bc6_unquantize(endpoints[first + e], n, signed); }
    }
    cursor = index_start;
    let groups = select(2u, 1u, mode >= 10u);
    let index_bits = select(3u, 4u, mode >= 10u);
    var index = 0u;
    for (var p = 0u; p <= texel; p++) {
        index = read_block(base, &cursor, index_bits - select(0u, 1u, (subset(partition_id, groups, p) & 4u) != 0u));
    }
    let pair = (subset(partition_id, groups, texel) & 1u) * 2u;
    let w = i32(weight(index_bits, index));
    var color = vec3f(0.0);
    for (var c = 0u; c < 3u; c++) {
        let value = (endpoints[c * 4u + pair] * (64 - w) + endpoints[c * 4u + pair + 1u] * w + 32) >> 6u;
        color[c] = bc6_half(value, signed);
    }
    return vec4f(color, 1.0);
}

)WGSL";
		return source;
	}
} // namespace fyuu_rhi::webgpu
