# WebGPU BC fallback verification — 2026-10-11

Configuration: Windows, MSVC 14.51.36231, Release, real NVIDIA and Intel GPUs.
The probe reports vendor IDs and never substitutes another adapter for a missing
explicitly selected test GPU.

After removing the runtime decoder override, the rebuilt suite passed **34/34**
in 75.53 seconds with two workers and no skipped tests. Native comparisons now
use separate devices with and without BC support, not a forced decoder flag.

Before removing the decoder override, the suite passed **37/37**, with no skipped
tests, in 83.62 seconds using two workers (177.77 seconds in the preceding serial run).
NVIDIA reported vendor 0x10de, driver 32.0.16.1742; Intel reported vendor 0x8086,
driver 32.0.101.7092. Neither successful result includes AMD hardware verification.

## Implemented formats

GPU software decoding covers BC1–BC7: linear/sRGB, BC4/BC5 UNorm/SNorm and
BC6H unsigned/signed. No public RHI interface changed.

Original compressed blocks remain in a buffer. Uploads, mip levels, array slices,
partial updates, copies and compressed readback preserve those bytes. Decoding
runs in the transfer's command encoder without CPU readback or separate submission.

| Format | Sampling storage |
| --- | --- |
| BC1–BC3, BC7 linear | RGBA8Unorm |
| BC1–BC3, BC7 sRGB | RGBA8UnormSrgb |
| NVIDIA BC4/BC5 | RGBA16Unorm / RGBA16Snorm |
| Intel BC4 | R16Unorm / R16Snorm |
| Intel BC5 | RG16Unorm / RG16Snorm |
| AMD BC4/BC5 | RGBA32Float |
| BC6H | RGBA16Float |

sRGB bytes are decoded into a linear RGBA8 temporary and copied into the sRGB
destination. This avoids an extra gamma conversion and compatibility-mode view
reinterpretation. Normalized16 formats require TextureFormatsTier1.
AMD's float32 path requires Float32Filterable; absent support is an explicit
error, not a change to sampler behavior or precision.

## Integer decoding

BC1–BC5 use vendor-specific endpoint/interpolation arithmetic. BC4/BC5 are not
rounded to BC3 alpha's eight-bit values. Float32 normalization is constructed
with integer division and nearest-even rounding: ordinary floating division
exposed a one-bit reciprocal error. AMD's unsigned normalized14 conversion also
accounts for the measured exceptional rounding inputs; signed values use exact
rational conversion. The profiles follow the author's measured
[GPU BCn decoding results](https://fgiesen.wordpress.com/2021/10/04/gpu-bcn-decoding/).

BC6H covers all 14 valid modes, signed endpoints, transformed deltas,
unquantization, both subset counts and anchor fix-ups. Reserved modes produce
opaque black. Conversion preserves the specified half-float values.

BC7 covers all eight modes, every partition shape, shared/per-endpoint P-bits,
component rotation, separate color/alpha index streams and anchor fix-ups.
A missing mode produces transparent black. Endpoint and interpolation operations
stay integer until storage conversion.

## Automatic fallback and remaining boundaries

When TextureCompressionBC is absent, BC1–BC7 software fallback is automatic on
NVIDIA, Intel and AMD profiles, without an experimental switch. BC6H/BC7 use vendor-independent,
specification-defined decoding and are also enabled on other adapters.
Aligned textures still use native BC when available. Odd-sized textures keep
their original sampling dimensions and original block storage.

AMD BC1–BC5 code is implemented but native filtering has not been verified on AMD
hardware here. This is a verification limitation, not a runtime opt-in requirement.
Unknown BC1–BC5 vendor profiles are rejected instead of using another vendor's arithmetic.
No precision downgrade or comparison tolerance was introduced.

FYUU_RHI_WEBGPU_DISABLE_BC=1 omits TextureCompressionBC at device creation.
It is a device-capability test setting, not a decoder selection switch.
Comparison tests create two devices on the same adapter: one with native BC,
one without it. The test uploads the native result's retained block bytes to the
second device and samples through that device's own scheduler and pipeline.
No resource crosses logical devices and no runtime decoder override remains.
Each device uses a fresh adapter object for the same vendor/device ID, because
Dawn consumes an adapter when it creates a device. The initial two-device test
incorrectly reused that adapter and failed before decoding; requesting a fresh
handle fixes the test without changing the backend. The three duplicate NVIDIA
automatic tests were removed because they now have the same configuration as
the software tests, leaving 34 registered tests.

## Real-GPU coverage

Native and software textures receive identical blocks on the same adapter's two devices.
Results are compared byte for byte, including float32 mantissa bits.

- Every RGB565 channel endpoint pair and BC1–BC3 color index.
- All 65,536 BC3 alpha endpoint pairs and all eight indices.
- All 65,536 BC4/BC5 endpoint byte pairs, every index, signed values and the
  redundant -128 encoding; BC5 uses opposite endpoint orders in its channels.
- Every BC6H/BC7 mode and partition, rotations, index-selection bits and reserved
  modes with deterministic varied block payloads.
- Three mips, one/two array layers, aligned and odd base dimensions.
- Nearest, fractional-coordinate linear and explicit-gradient 16× anisotropic
  sampling into RGBA32Float; BC1–BC3 also cover linear/sRGB RGBA8 targets.
- Original-block copies/readback, padded rows, offsets, partial updates and
  a 2287 × 2279 image.
- Feature-absent software reference vectors and automatic fallback selected only
  by the device's native BC capability.

Before Intel registration, the NVIDIA 26-test suite passed.
Intel tests found a real representation difference: RGBA16 was exact for nearest
and bilinear sampling but differed by one float32 ULP under BC4/BC5 anisotropy.
RGBA32Float also failed bilinear comparison. R16/RG16 normalized storage passed
both comparisons; neither rejected representation is used for Intel.

An explicitly requested unavailable adapter returns 77; registered Intel tests
mark this as skipped, not passed or verified.

~~~text
ctest --test-dir cmake-build/MSVC_Visual_Studio_2026_vcpkg -C Release -R "^BCCopyProbe\." --output-on-failure
BCCopyProbe.exe webgpu adapters
BCCopyProbe.exe webgpu compare.channels 0x8086
BCCopyProbe.exe webgpu compare.bptc.aniso 0x8086
~~~

Coverage applies to the tested adapters and drivers, not every vendor, driver or
sampler configuration. AMD hardware verification remains outstanding and is not
included in successful native-comparison results.
