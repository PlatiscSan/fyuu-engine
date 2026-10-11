#include <version>
#if defined(__cpp_lib_modules)
import std;
#else
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <utility>

#include <vector>

#include <algorithm>

#include <cstring>
#include <string>

#include <iostream>

#include <cstdint>

#include <array>

#include <atomic>
#include <future>

#include <string_view>

#include <source_location>

#include <ranges>
#include <span>
#endif
#if defined(_WIN32)
#include <Windows.h>
#endif // defined(_WIN32)
import fyuu_rhi;

namespace {
	using namespace fyuu_rhi;
	using namespace fyuu_rhi::execution;

	/** @brief Rejects native validation errors as well as copy mismatches. */
	struct Sink final : log::Sink {
		std::atomic_bool failed = false;
		void Write(log::Level level, std::string_view message, std::source_location const&) noexcept override {
			if (level == log::Level::Error || level == log::Level::Fatal) {
				failed.store(true);
				std::cerr << message << '\n';
			}
		}
	};

	struct Receiver {
		std::promise<CommandGraphResources>* result;
		struct Environment {};
		Environment get_env() const noexcept { return {}; }
		void RecoverBindings(CommandGraphResources&&) noexcept {}
		void set_value(CommandGraphResources&& resources) && noexcept {
			result->set_value(std::move(resources));
		}
		void set_error(std::exception_ptr error) && noexcept { result->set_exception(error); }
		void set_stopped() && noexcept {
			result->set_exception(std::make_exception_ptr(std::runtime_error("BC copy stopped")));
		}
	};

	/** @brief Uploads the native comparison's final blocks to a device created without BC support.
	 * @note Resources never cross logical devices; the CPU block readback belongs only to this test.
	 */
	Resource UploadComparisonTexture(
		LogicalDevice* device,
		CommandScheduler const* scheduler,
		Resource&& texture,
		std::span<std::byte const> bytes,
		std::size_t offset,
		std::uint32_t pitch,
		std::uint32_t rows,
		std::size_t slice_size
	) {
		auto extent = texture.GetTextureExtent();
		ResourceFlags flags;
		flags.Set(ResourceFlagBits::HostVisible);
		flags.Set(ResourceFlagBits::CopySRC);
		auto upload = device->CreateBuffer(bytes.size(), flags);
		auto graph = scheduler->schedule();
		auto source = graph.RegisterResource();
		auto image = graph.RegisterResource();
		auto node = graph.CreateNode(QueueType::Transfer);
		node.Access({source, AccessMode::Read, ResourceUsage::CopySource, {}})
			.Access({image, AccessMode::Write, ResourceUsage::CopyDestination, {}})
			.Record(WriteBuffer{source, 0u, std::vector<std::byte>(bytes.begin(), bytes.end())});
		for (std::uint32_t mip = 0u; mip < extent.mip_levels; ++mip) {
			node.Record(
				CopyBufferToTexture{
					source, image, {offset + mip * slice_size, pitch, rows},
					{.mip_level = mip, .array_layer_count = extent.depth_or_array_layers,
					 .width = (std::max)(extent.width >> mip, 1u),
					 .height = (std::max)(extent.height >> mip, 1u)}
				}
			);
		}
		std::promise<CommandGraphResources> result;
		auto future = result.get_future();
		auto operation = std::move(graph).connect(Receiver{&result});
		operation.BindResource(source, std::move(upload));
		operation.BindResource(image, std::move(texture));
		operation.start();
		return future.get().TakeResource(image);
	}

	/** @brief Samples the fallback through the public API, including its last texel.
	 * BC1 blocks encode opaque red; the sRGB case verifies conversion of mid red.
	 */
	Resource CheckSample(
		LogicalDevice* device,
		CommandScheduler const* scheduler,
		Resource&& texture,
		ResourceFlags const& texture_flags,
		std::uint32_t width,
		std::uint32_t height,
		bool srgb_target = false,
		std::span<std::byte const> expected = {},
		std::vector<std::byte>* captured = nullptr,
		bool linear = false,
		bool floating_target = false,
		std::uint32_t mip = 0u,
		std::uint32_t layer = 0u,
		bool array_texture = false,
		bool anisotropic = false
	) {
		using namespace pipeline;
		constexpr char shader[] = R"(
[[vk::binding(0,0)]] Texture2D image : register(t0, space0);
[[vk::binding(1,0)]] SamplerState nearest : register(s1, space0);
[shader("vertex")]
float4 vs(uint index : SV_VertexID) : SV_Position {
    float2 positions[3] = {float2(-1,-1), float2(3,-1), float2(-1,3)};
    return float4(positions[index], 0, 1);
}
[shader("fragment")]
float4 fs(float4 position : SV_Position) : SV_Target0 {
    uint width, height; image.GetDimensions(width, height);
    return image.SampleLevel(nearest, position.xy / float2(width,height), 0);
}
)";
		std::string shader_code(shader);
		if (array_texture) {
			/// @brief Compatibility mode requires array textures to be sampled through array views.
			shader_code.replace(shader_code.find("Texture2D image"), 15u, "Texture2DArray image");
			std::string_view dimensions = "uint width, height; image.GetDimensions(width, height);";
			shader_code.replace(
				shader_code.find(dimensions), dimensions.size(),
				"uint width, height, layers; image.GetDimensions(width, height, layers);"
			);
			std::string_view coordinates = "position.xy / float2(width,height)";
			shader_code.replace(
				shader_code.find(coordinates), coordinates.size(),
				"float3(position.xy / float2(width,height), " + std::to_string(layer) + ")"
			);
		}
		if (linear) {
			/// @brief Fractional texel coordinates ensure linear filtering is actually exercised.
			auto position = shader_code.find("position.xy /");
			shader_code.replace(position, 11u, "(position.xy + 0.25)");
		}
		if (anisotropic) {
			/// @brief An 8:1 explicit gradient exercises anisotropic filtering without relying on screen derivatives.
			shader_code.replace(shader_code.find("SampleLevel"), 11u, "SampleGrad");
			shader_code.replace(
				shader_code.find(", 0);"), 5u,
				", float2(8.0 / width, 0), float2(0, 1.0 / height));"
			);
		}
		std::array modules{SlangPipelineProgramDescriptor::Module{"bc_sample_probe", shader_code}};
		std::array entries{
			SlangPipelineProgramDescriptor::EntryPoint{"vs", Stage::Vertex},
			SlangPipelineProgramDescriptor::EntryPoint{"fs", Stage::Fragment}
		};
		auto target_format = floating_target ? ResourceFlagBits::R32G32B32A32Float : srgb_target ? ResourceFlagBits::R8G8B8A8Srgb :
			ResourceFlagBits::R8G8B8A8Unorm;
		std::array targets{ColorTargetState{.format = target_format}};
		auto pipeline = device->CreateGraphicsPipeline(
			{.program = {.modules = modules, .entry_points = entries}, .color_targets = targets}
		);
		auto view_flags = texture_flags;
		if (array_texture) {
			view_flags.Reset(ResourceFlagBits::TextureView2D);
			view_flags.Set(ResourceFlagBits::TextureView2DArray);
		}
		auto texture_view = texture.CreateTextureView(
			mip,
			1u,
			array_texture ? 0u : layer,
			array_texture ? texture.GetTextureExtent().depth_or_array_layers : 1u,
			view_flags
		);
		auto sampler = device->CreateSampler(
			{.address_mode_u = AddressMode::ClampToEdge, .address_mode_v = AddressMode::ClampToEdge,
			 .address_mode_w = AddressMode::ClampToEdge,
			 .mag_filter = linear ? FilterMode::Linear : FilterMode::Nearest,
			 .min_filter = linear ? FilterMode::Linear : FilterMode::Nearest,
			 .mipmap_filter = anisotropic ? MipmapFilterMode::Linear : MipmapFilterMode::Nearest,
			 .max_anisotropy = static_cast<std::uint8_t>(anisotropic ? 16u : 1u)}
		);
		std::array bindings{
			ResourceBinding{.slot = 0u, .value = BindingValue::FromView(texture_view)},
			ResourceBinding{.slot = 1u, .value = BindingValue::FromSampler(sampler)}
		};
		auto group = pipeline.CreatePipelineResourceGroup(0u, bindings);
		ResourceFlags flags;
		for (auto bit : {ResourceFlagBits::Texture2D, ResourceFlagBits::TextureView2D,
			ResourceFlagBits::TextureViewAspectAll, target_format,
			ResourceFlagBits::RenderAttachment, ResourceFlagBits::CopySRC}) {
			flags.Set(bit);
		}
		auto target = device->CreateTexture(width, height, 1u, 1u, flags);
		auto view = target.CreateTextureView(0u, 1u, 0u, 1u, flags);
		auto pixel_bytes = floating_target ? 16u : 4u;
		auto pitch = (width * pixel_bytes + 255u) / 256u * 256u;
		flags = {};
		flags.Set(ResourceFlagBits::DeviceReadback);
		flags.Set(ResourceFlagBits::CopyDST);
		auto readback = device->CreateBuffer(static_cast<std::size_t>(pitch) * height, flags);
		auto graph = scheduler->schedule();
		auto image = graph.RegisterResource();
		auto color = graph.RegisterResource();
		auto buffer = graph.RegisterResource();
		auto color_view = graph.RegisterView();
		auto sampled_view = graph.RegisterView();
		auto sampler_binding = graph.RegisterSampler();
		auto pipeline_binding = graph.RegisterPipeline();
		auto group_binding = graph.RegisterResourceGroup();
		auto draw = graph.CreateNode(QueueType::Graphics);
		draw.Access({image, AccessMode::Read, ResourceUsage::Sampled, {}})
			.Access({color, AccessMode::Write, ResourceUsage::ColorAttachment, {}})
			.Record(BindPipeline{pipeline_binding})
			.Record(BeginRendering{.area = {0, 0, width, height}, .colors = {{.resource = color, .view = color_view}}})
			.Record(BindResourceGroup{group_binding, 0u})
			.Record(Draw{3u})
			.Record(EndRendering{});
		graph.CreateNode(QueueType::Transfer, draw)
			.Access({color, AccessMode::Read, ResourceUsage::CopySource, {}})
			.Access({buffer, AccessMode::Write, ResourceUsage::CopyDestination, {}})
			.Record(CopyTextureToBuffer{color, buffer, {.width = width, .height = height}, {0u, pitch, height}});
		std::promise<CommandGraphResources> result;
		auto future = result.get_future();
		auto operation = std::move(graph).connect(Receiver{&result});
		operation.BindResource(image, std::move(texture));
		operation.BindResource(color, std::move(target));
		operation.BindResource(buffer, std::move(readback));
		operation.BindView(color_view, std::move(view));
		operation.BindView(sampled_view, std::move(texture_view));
		operation.BindSampler(sampler_binding, std::move(sampler));
		operation.BindPipeline(pipeline_binding, std::move(pipeline));
		operation.BindResourceGroup(group_binding, std::move(group));
		operation.start();
		auto resources = future.get();
		readback = resources.TakeResource(buffer);
		auto mapping = readback.Map({0u, static_cast<std::size_t>(pitch) * height});
		if (captured) {
			captured->resize(static_cast<std::size_t>(width) * height * pixel_bytes);
			for (std::uint32_t y = 0u; y < height; ++y) {
				std::ranges::copy(
					mapping.Read().subspan(static_cast<std::size_t>(y) * pitch, width * pixel_bytes),
					captured->begin() + static_cast<std::size_t>(y) * width * pixel_bytes
				);
			}
			return resources.TakeResource(image);
		}
		for (std::uint32_t y = 0u; y < height; ++y) {
			for (std::uint32_t x = 0u; x < width; ++x) {
				auto texel = mapping.Read().subspan(static_cast<std::size_t>(y) * pitch + x * 4u, 4u);
				if (!expected.empty()) {
					auto reference = expected.subspan((static_cast<std::size_t>(y) * width + x) * 4u, 4u);
					if (!std::ranges::equal(texel, reference)) {
						std::cerr << "BC decode mismatch at " << x << ',' << y << ": ";
						for (std::size_t channel = 0u; channel < 4u; ++channel) {
							std::cerr << std::to_integer<unsigned>(texel[channel]) << '/'
								<< std::to_integer<unsigned>(reference[channel]) << ' ';
						}
						std::cerr << '\n';
						throw std::runtime_error("BC integer decoder differs from reference texels");
					}
					continue;
				}
				bool srgb = texture_flags.Test(ResourceFlagBits::Bc1UnormSrgb) ||
					texture_flags.Test(ResourceFlagBits::Bc2UnormSrgb) ||
					texture_flags.Test(ResourceFlagBits::Bc3UnormSrgb);
				auto expected_red = srgb ?
					(srgb_target ? std::byte{132} : std::byte{59}) : std::byte{255};
				if (texel[0] != expected_red || texel[1] != std::byte{0} ||
					texel[2] != std::byte{0} || texel[3] != std::byte{255}) {
					throw std::runtime_error("Decoded BC sampling differs from expected red");
				}
			}
		}
		if (!srgb_target && expected.empty()) {
			/** @brief Exercise both linear and sRGB destinations with the same sampled BC image. */
			return CheckSample(
				device,
				scheduler,
				resources.TakeResource(image),
				texture_flags,
				width,
				height,
				true
			);
		}
		return resources.TakeResource(image);
	}

	/** @brief Generates BC1–BC3 palette/index vectors and independently expected
	 * RGBA8 texels, including transparent BC1 and both BC3 alpha endpoint orders.
	 */
	std::vector<std::byte> MakeDecodeVectors(
		std::span<std::byte> bytes,
		std::size_t offset,
		std::uint32_t pitch,
		std::uint32_t width,
		std::uint32_t height,
		std::uint32_t kind,
		bool nvidia,
		bool intel
	) {
		auto block_bytes = kind == 1u ? 8u : 16u;
		std::vector<std::byte> expected(static_cast<std::size_t>(width) * height * 4u);
		for (std::uint32_t by = 0u; by < (height + 3u) / 4u; ++by) {
			for (std::uint32_t bx = 0u; bx < (width + 3u) / 4u; ++bx) {
				auto block = bytes.subspan(offset + by * pitch + bx * block_bytes, block_bytes);
				std::ranges::fill(block, std::byte{0});
				bool reverse = ((bx + by) & 1u) != 0u;
				std::uint16_t endpoints[]{
					static_cast<std::uint16_t>(reverse ? 0x19b3u : 0xed46u),
					static_cast<std::uint16_t>(reverse ? 0xed46u : 0x19b3u)
				};
				if (width == 256u && height == 256u) {
					/// @brief Cover every RGB565 channel endpoint pair and palette index.
					endpoints[0] = static_cast<std::uint16_t>(((bx & 31u) << 11u) | (bx << 5u) | (bx & 31u));
					endpoints[1] = static_cast<std::uint16_t>(((by & 31u) << 11u) | (by << 5u) | (by & 31u));
					reverse = endpoints[0] <= endpoints[1];
				}
				auto color_offset = kind == 1u ? 0u : 8u;
				for (std::uint32_t e = 0u; e < 2u; ++e) {
					block[color_offset + e * 2u] = std::byte(endpoints[e] & 255u);
					block[color_offset + e * 2u + 1u] = std::byte(endpoints[e] >> 8u);
				}
				std::uint32_t color_indices = 0u;
				std::uint64_t alpha_data = 0u;
				auto a0 = reverse ? 10u : 240u;
				auto a1 = reverse ? 230u : 20u;
				if (width == 1024u && height == 1024u) {
					/// @brief Every block exercises all eight indices for one alpha endpoint pair.
					a0 = bx;
					a1 = by;
				}
				if (kind == 3u) {
					alpha_data = a0 | (a1 << 8u);
				}
				for (std::uint32_t p = 0u; p < 16u; ++p) {
					auto index = p % 4u;
					color_indices |= index << (p * 2u);
					auto ai = p % 8u;
					auto alpha = 255u;
					if (kind == 2u) {
						alpha_data |= static_cast<std::uint64_t>(p) << (p * 4u);
						alpha = p * 17u;
					}
					if (kind == 3u) {
						alpha_data |= static_cast<std::uint64_t>(ai) << (16u + p * 3u);
						alpha = ai == 0u ? a0 : ai == 1u ? a1 :
							a0 > a1 ? ((8u - ai) * a0 + (ai - 1u) * a1 + 3u) / 7u :
							ai < 6u ? ((6u - ai) * a0 + (ai - 1u) * a1 + 2u) / 5u :
							ai == 6u ? 0u : 255u;
						if (nvidia && ai >= 2u && (a0 > a1 || ai < 6u)) {
							auto anchor = static_cast<int>(a0);
							auto delta = static_cast<int>(a1) - anchor;
							int multiple = 1;
							if (a0 > a1) {
								if (ai == 3u || ai == 6u) { multiple = 2; }
								if (ai == 4u || ai == 5u) { multiple = 4; }
								if (ai == 4u || ai >= 6u) { anchor = static_cast<int>(a1); delta = -delta; }
							}
							else {
								if (ai == 3u || ai == 4u) { multiple = 2; }
								if (ai >= 4u) { anchor = static_cast<int>(a1); delta = -delta; }
							}
							auto product = delta * multiple;
							auto scale = a0 > a1 ? 36 : 48;
							alpha = static_cast<unsigned>(anchor + ((scale * product + (product >> 3) + 128) >> 8));
						}
						if (intel && ai >= 2u && (a0 > a1 || ai < 6u)) {
							auto denominator = a0 > a1 ? 7u : 5u;
							auto weight = ((ai - 1u) * 256u + denominator / 2u) / denominator;
							alpha = ((256u - weight) * a0 + weight * a1 + 128u) >> 8u;
						}
					}
					auto Expand = [](std::uint16_t value) {
						auto r = (value >> 11u) & 31u;
						auto g = (value >> 5u) & 63u;
						auto b = value & 31u;
						return std::array<std::uint32_t, 3u>{
							(r << 3u) | (r >> 2u), (g << 2u) | (g >> 4u), (b << 3u) | (b >> 2u)
						};
					};
					auto c0 = Expand(endpoints[0]);
					auto c1 = Expand(endpoints[1]);
					auto color = index == 1u ? c1 : c0;
					if (index >= 2u) {
						for (std::size_t c = 0u; c < 3u; ++c) {
							color[c] = kind != 1u || !reverse ?
								(index == 2u ? (2u * c0[c] + c1[c]) / 3u : (c0[c] + 2u * c1[c]) / 3u) :
								(index == 2u ? (c0[c] + c1[c]) / 2u : 0u);
						}
						if (kind == 1u && reverse && index == 3u) {
							alpha = 0u;
						}
						else if (intel) {
							auto weight = kind == 1u && reverse ? 128u : index == 2u ? 85u : 171u;
							for (std::size_t c = 0u; c < 3u; ++c) {
								color[c] = ((256u - weight) * c0[c] + weight * c1[c] + 128u) >> 8u;
							}
						}
						else if (nvidia) {
							auto r0 = (endpoints[0] >> 11u) & 31u;
							auto b0 = endpoints[0] & 31u;
							auto r1 = (endpoints[1] >> 11u) & 31u;
							auto b1 = endpoints[1] & 31u;
							auto g0 = static_cast<int>(c0[1]);
							auto g1 = static_cast<int>(c1[1]);
							auto delta = g1 - g0;
							if (kind != 1u || !reverse) {
								auto weight = index == 2u ? 2u : 1u;
								color[0] = ((weight * r0 + (3u - weight) * r1) * 22u) >> 3u;
								color[2] = ((weight * b0 + (3u - weight) * b1) * 22u) >> 3u;
								auto scaled = 80 * delta + (delta >> 2);
								color[1] = static_cast<unsigned>(index == 2u ?
									g0 + ((128 + scaled) >> 8) : g1 + ((128 - scaled) >> 8));
							}
							else {
								color[0] = ((r0 + r1) * 33u) >> 3u;
								color[2] = ((b0 + b1) * 33u) >> 3u;
								color[1] = static_cast<unsigned>(g0 + ((128 + 128 * delta + (delta >> 2)) >> 8));
							}
						}
					}
					auto x = bx * 4u + p % 4u;
					auto y = by * 4u + p / 4u;
					if (x < width && y < height) {
						auto out = expected.data() + (static_cast<std::size_t>(y) * width + x) * 4u;
						out[0] = std::byte(color[0]);
						out[1] = std::byte(color[1]);
						out[2] = std::byte(color[2]);
						out[3] = std::byte(alpha);
					}
				}
				for (std::uint32_t b = 0u; b < 4u; ++b) {
					block[color_offset + 4u + b] = std::byte((color_indices >> (b * 8u)) & 255u);
				}
				if (kind != 1u) {
					for (std::uint32_t b = 0u; b < 8u; ++b) {
						block[b] = std::byte((alpha_data >> (b * 8u)) & 255u);
					}
				}
			}
		}
		return expected;
	}
}

/** @brief Exercises both BC block sizes, padded rows, odd edges, offsets and array slices. */
int main(int argc, char** argv) try {
	using Bits = ResourceFlagBits;
	static Sink sink;
	auto name = argc > 1 ? std::string_view(argv[1]) : "vulkan";
	auto backend = name == "opengl" ? Backend::OpenGL :
		name == "webgpu" ? Backend::WebGPU : Backend::Vulkan;
	InitializeRHIContext("BC copy probe", {}, "FyuuRHI", {}, &sink);
	auto devices = RequestInstance(backend).EnumeratePhysicalDevices();
	auto mode = argc > 2 ? std::string_view(argv[2]) : std::string_view{};
	if (mode == "adapters") {
		for (auto const& adapter : devices) {
			auto info = adapter.GetInfo();
			std::cout << info.name << " vendor 0x" << std::hex << info.vendor_id.value_or(0u) << std::dec << '\n';
		}
		return 0;
	}
	PhysicalDevice const* physical_device = &BestPerformance(devices);
	if (argc > 3) {
		/// @brief An explicit vendor selector never substitutes another adapter for a missing test GPU.
		auto vendor = std::stoul(argv[3], nullptr, 0);
		auto selected = std::ranges::find_if(
			devices,
			[vendor](auto const& adapter) {
				return adapter.GetInfo().vendor_id.value_or(0u) == vendor;
			}
		);
		if (selected == devices.end()) {
			std::cerr << "Requested BC test adapter is unavailable; test was not executed\n";
			return 77;
		}
		physical_device = &*selected;
	}
	auto adapter_info = physical_device->GetInfo();
	std::cout << adapter_info.name << " vendor 0x" << std::hex
		<< adapter_info.vendor_id.value_or(0u) << std::dec << '\n';
	bool nvidia = adapter_info.vendor_id.value_or(0u) == 0x10deu;
	bool intel = adapter_info.vendor_id.value_or(0u) == 0x8086u;
	auto device = physical_device->CreateLogicalDevice();
	auto scheduler = device.CreateScheduler();
	constexpr std::string_view modes[]{
		"", "large", "aligned", "decode", "software", "software.aligned",
		"software.large", "software.channels", "compare", "compare.aligned", "compare.palette", "compare.alpha", "compare.channels",
		"compare.bptc", "compare.bptc.odd", "software.bptc", "compare.palette.float", "compare.alpha.float",
		"compare.palette.aniso", "compare.channels.aniso", "compare.bptc.aniso"
	};
	if (std::ranges::find(modes, mode) == std::ranges::end(modes)) {
		throw std::invalid_argument("Unknown BC probe mode");
	}
	auto large = mode == "large" || mode == "software.large";
	auto aligned = mode == "aligned" || mode == "software.aligned" || mode == "compare.aligned";
	auto software = mode.starts_with("software");
	auto compare = mode.starts_with("compare");
	auto decode = software || compare || mode == "decode";
	LogicalDevice software_device;
	CommandScheduler software_scheduler;
#if defined(_WIN32)
	if (compare && backend == Backend::WebGPU) {
		/// @brief Compare native support against a second device without BC, not a runtime decoder override.
		if (!SetEnvironmentVariableA("FYUU_RHI_WEBGPU_DISABLE_BC", "1")) {
			throw std::runtime_error("Failed to disable BC on the comparison device");
		}
		try {
			/// @brief Dawn consumes each adapter object when creating its device; request a fresh handle to the same GPU.
			auto software_adapters = RequestInstance(backend).EnumeratePhysicalDevices();
			auto selected = std::ranges::find_if(
				software_adapters,
				[&adapter_info](auto const& adapter) {
					auto info = adapter.GetInfo();
					return info.vendor_id == adapter_info.vendor_id && info.device_id == adapter_info.device_id;
				}
			);
			if (selected == software_adapters.end()) {
				throw std::runtime_error("Failed to request the same GPU for the software comparison");
			}
			software_device = selected->CreateLogicalDevice();
		}
		catch (...) {
			(void)SetEnvironmentVariableA("FYUU_RHI_WEBGPU_DISABLE_BC", "0");
			throw;
		}
		if (!SetEnvironmentVariableA("FYUU_RHI_WEBGPU_DISABLE_BC", "0")) {
			throw std::runtime_error("Failed to restore BC device creation configuration");
		}
		software_scheduler = software_device.CreateScheduler();
	}
#endif // defined(_WIN32)
	for (auto format : {Bits::Bc1Unorm, Bits::Bc1UnormSrgb, Bits::Bc2Unorm, Bits::Bc2UnormSrgb,
		Bits::Bc3Unorm, Bits::Bc3UnormSrgb, Bits::Bc4Unorm, Bits::Bc4Snorm, Bits::Bc5Unorm,
		Bits::Bc5Snorm, Bits::Bc6HUfloat, Bits::Bc6HSfloat, Bits::Bc7Unorm, Bits::Bc7UnormSrgb}) {
		bool channels = mode.starts_with("compare.channels") || mode == "software.channels";
		bool bptc = mode.starts_with("compare.bptc") || mode == "software.bptc";
		bool bptc_format = format == Bits::Bc6HUfloat || format == Bits::Bc6HSfloat ||
			format == Bits::Bc7Unorm || format == Bits::Bc7UnormSrgb;
		bool channel_format = format == Bits::Bc4Unorm || format == Bits::Bc4Snorm ||
			format == Bits::Bc5Unorm || format == Bits::Bc5Snorm;
		if (channels && !channel_format) {
			continue;
		}
		if (bptc && !bptc_format) {
			continue;
		}
		if (!channels && !bptc && (software || compare) && format != Bits::Bc1Unorm && format != Bits::Bc1UnormSrgb &&
			format != Bits::Bc2Unorm && format != Bits::Bc2UnormSrgb &&
			format != Bits::Bc3Unorm && format != Bits::Bc3UnormSrgb) {
			continue;
		}
		for (std::uint32_t layers : {1u, 2u}) {
			if ((channels || (bptc && software)) && layers != 1u) {
				continue;
			}
			if (mode.starts_with("compare.alpha") &&
				(layers != 1u || (format != Bits::Bc3Unorm && format != Bits::Bc3UnormSrgb))) {
				continue;
			}
			auto block_bytes = format == Bits::Bc1Unorm || format == Bits::Bc1UnormSrgb ||
				format == Bits::Bc4Unorm || format == Bits::Bc4Snorm ? 8u : 16u;
			auto width = bptc ? (mode == "compare.bptc.odd" ? 511u : 512u) :
				channels || mode.starts_with("compare.alpha") ? 1024u : mode.starts_with("compare.palette") ? 256u : large ? 2287u : aligned ? 8u : 7u;
			auto height = bptc ? (mode == "compare.bptc.odd" ? 509u : 512u) :
				channels || mode.starts_with("compare.alpha") ? 1024u : mode.starts_with("compare.palette") ? 256u : large ? 2279u : aligned ? 8u : 5u;
			auto block_columns = (width + 3u) / 4u;
			auto tight_pitch = block_columns * block_bytes;
			auto pitch = backend == Backend::OpenGL ? tight_pitch : (tight_pitch + 255u) / 256u * 256u;
			auto rows = (height + 3u) / 4u;
			constexpr std::size_t offset = 512u;
			auto mip_count = backend == Backend::OpenGL ? 1u : 3u;
			auto slice_size = static_cast<std::size_t>(pitch) * rows * layers;
			auto size = offset + slice_size * mip_count;
			std::vector<std::byte> bytes(size);
			for (std::uint32_t layer = 0u; layer < layers; ++layer) {
				for (std::uint32_t row = 0u; row < rows; ++row) {
					for (std::uint32_t column = 0u; column < tight_pitch; ++column) {
						bytes[offset + (layer * rows + row) * pitch + column] =
							std::byte((layer * 71u + row * 31u + column) & 255u);
					}
				}
			}
			bool sample = (layers == 1u || bptc) && (format == Bits::Bc1Unorm || format == Bits::Bc1UnormSrgb || software || compare);
			if (sample) {
				for (std::uint32_t row = 0u; row < rows; ++row) {
					for (std::uint32_t block = 0u; block < block_columns; ++block) {
						auto begin = bytes.begin() + offset + row * pitch + block * block_bytes;
						std::fill_n(begin, block_bytes, std::byte{0});
						bool srgb = format == Bits::Bc1UnormSrgb || format == Bits::Bc2UnormSrgb ||
							format == Bits::Bc3UnormSrgb;
						auto color_offset = format == Bits::Bc1Unorm || format == Bits::Bc1UnormSrgb ? 0u : 8u;
						begin[color_offset + 1u] = srgb ? std::byte{0x80} : std::byte{0xf8};
						if (format == Bits::Bc2Unorm || format == Bits::Bc2UnormSrgb) {
							std::fill_n(begin, 8u, std::byte{255});
						}
						if (format == Bits::Bc3Unorm || format == Bits::Bc3UnormSrgb) {
							begin[0] = std::byte{255};
						}
					}
				}
			}
			std::vector<std::byte> expected;
			if (decode && layers == 1u &&
				(format == Bits::Bc1Unorm || format == Bits::Bc2Unorm || format == Bits::Bc3Unorm ||
					(compare && (format == Bits::Bc1UnormSrgb || format == Bits::Bc2UnormSrgb || format == Bits::Bc3UnormSrgb)))) {
				auto kind = format == Bits::Bc1Unorm || format == Bits::Bc1UnormSrgb ? 1u :
					format == Bits::Bc2Unorm || format == Bits::Bc2UnormSrgb ? 2u : 3u;
				expected = MakeDecodeVectors(bytes, offset, pitch, width, height, kind, nvidia, intel);
				sample = true;
			}
			ResourceFlags flags;
			if (bptc) {
				/// @brief Deterministic block payloads cover every mode, reserved modes and partition combinations.
				constexpr unsigned modes6[]{0u, 1u, 2u, 6u, 10u, 14u, 18u, 22u, 26u, 30u, 3u, 7u, 11u, 15u, 19u, 23u, 27u, 31u};
				for (std::uint32_t by = 0u; by < rows; ++by) {
					for (std::uint32_t bx = 0u; bx < block_columns; ++bx) {
						auto block = bytes.data() + offset + by * pitch + bx * 16u;
						auto sequence = by * block_columns + bx;
						std::uint32_t random = 0x4d595df4u ^ (sequence * 2654435761u);
						for (unsigned b = 0u; b < 16u; ++b) {
							random ^= random << 13u;
							random ^= random >> 17u;
							random ^= random << 5u;
							block[b] = software ? std::byte{0} : std::byte(random & 255u);
						}
						auto SetBits = [block](unsigned at, unsigned count, unsigned value) {
							for (unsigned i = 0u; i < count; ++i) {
								auto bit = at + i;
								auto old = std::to_integer<unsigned>(block[bit / 8u]);
								block[bit / 8u] = std::byte((old & ~(1u << (bit % 8u))) |
									(((value >> i) & 1u) << (bit % 8u)));
							}
						};
						if (format == Bits::Bc6HUfloat || format == Bits::Bc6HSfloat) {
							auto encoded = modes6[sequence % std::size(modes6)];
							auto mask = encoded < 2u ? 3u : 31u;
							block[0] = std::byte((std::to_integer<unsigned>(block[0]) & ~mask) | encoded);
							if (!software && sequence % std::size(modes6) < 10u) {
								SetBits(77u, 5u, sequence / std::size(modes6) % 32u);
							}
						}
						else {
							auto encoded = sequence % 9u;
							auto mask = encoded == 8u ? 255u : (1u << (encoded + 1u)) - 1u;
							block[0] = std::byte((std::to_integer<unsigned>(block[0]) & ~mask) | (encoded == 8u ? 0u : 1u << encoded));
							if (!software) {
								auto trial = sequence / 9u;
								if (encoded == 0u || encoded == 1u || encoded == 2u || encoded == 3u || encoded == 7u) {
									SetBits(encoded + 1u, encoded == 0u ? 4u : 6u, trial % 64u);
								}
								if (encoded == 4u || encoded == 5u) {
									SetBits(encoded + 1u, 2u, trial / 64u % 4u);
									if (encoded == 4u) {
										SetBits(7u, 1u, trial / 256u % 2u);
									}
								}
							}
						}
					}
				}
			}
			if (channels) {
				/// @brief Cover both signed and unsigned endpoint bytes and every three-bit index.
				for (std::uint32_t by = 0u; by < rows; ++by) {
					for (std::uint32_t bx = 0u; bx < block_columns; ++bx) {
						for (std::uint32_t c = 0u; c < block_bytes / 8u; ++c) {
							std::uint64_t block = c == 0u ? bx | (by << 8u) : by | (bx << 8u);
							for (std::uint32_t p = 0u; p < 16u; ++p) {
								block |= static_cast<std::uint64_t>(p % 8u) << (16u + p * 3u);
							}
							for (std::uint32_t b = 0u; b < 8u; ++b) {
								bytes[offset + by * pitch + bx * block_bytes + c * 8u + b] = std::byte((block >> (b * 8u)) & 255u);
							}
						}
					}
				}
			}
			flags.Set(Bits::HostVisible);
			flags.Set(Bits::CopySRC);
			auto upload = device.CreateBuffer(size, flags);
			flags = {};
			flags.Set(Bits::DeviceReadback);
			flags.Set(Bits::CopyDST);
			auto readback = device.CreateBuffer(size, flags);
			flags = {};
			flags.Set(Bits::DeviceLocal);
			flags.Set(Bits::CopySRC);
			flags.Set(Bits::CopyDST);
			flags.Set(Bits::TextureBinding);
			flags.Set(Bits::Texture2D);
			flags.Set(Bits::TextureView2D);
			flags.Set(Bits::TextureViewAspectAll);
			flags.Set(format);
			auto texture = device.CreateTexture(width, height, layers, mip_count, flags);
			auto cloned = device.CreateTexture(width, height, layers, mip_count, flags);
			auto graph = scheduler.schedule();
			auto source = graph.RegisterResource();
			auto image = graph.RegisterResource();
			auto destination = graph.RegisterResource();
			auto clone = graph.RegisterResource();
			TextureDataLayout layout{offset, pitch, rows};
			TextureRegion region{.array_layer_count = layers, .width = width, .height = height};
			auto write = graph.CreateNode(QueueType::Transfer);
			write.Access({source, AccessMode::Read, ResourceUsage::CopySource, {}})
				.Access({image, AccessMode::Write, ResourceUsage::CopyDestination, {}})
				.Record(WriteBuffer{source, 0u, bytes});
			for (std::uint32_t mip = 0u; mip < mip_count; ++mip) {
				region.mip_level = mip;
				region.width = (std::max)(width >> mip, 1u);
				region.height = (std::max)(height >> mip, 1u);
				write.Record(CopyBufferToTexture{source, image, layout, region});
			}
			auto copy = graph.CreateNode(QueueType::Transfer, write);
			copy.Access({image, AccessMode::Read, ResourceUsage::CopySource, {}})
				.Access({clone, AccessMode::Write, ResourceUsage::CopyDestination, {}});
			for (std::uint32_t mip = 0u; mip < mip_count; ++mip) {
				region.mip_level = mip;
				region.width = (std::max)(width >> mip, 1u);
				region.height = (std::max)(height >> mip, 1u);
				copy.Record(CopyTexture{image, clone, region, region});
			}
			/// @brief Updating only the last block must preserve the other blocks,
			/// slices and mip levels in both the raw and sampled representations.
			auto last = copy;
			if (backend == Backend::WebGPU) {
				auto update = graph.CreateNode(QueueType::Transfer, copy);
				update.Access({source, AccessMode::Read, ResourceUsage::CopySource, {}})
					.Access({clone, AccessMode::Write, ResourceUsage::CopyDestination, {}});
				auto x = (width - 1u) / 4u * 4u;
				auto y = (height - 1u) / 4u * 4u;
				update.Record(
					CopyBufferToTexture{
						source, clone,
						{offset + (static_cast<std::size_t>(layers - 1u) * rows + y / 4u) * pitch + x / 4u * block_bytes,
							pitch, rows},
						{.base_array_layer = layers - 1u, .offset_x = x, .offset_y = y,
							.width = width - x, .height = height - y}
					}
				);
				last = update;
			}
			auto read = graph.CreateNode(QueueType::Transfer, last);
			read.Access({clone, AccessMode::Read, ResourceUsage::CopySource, {}})
				.Access({destination, AccessMode::Write, ResourceUsage::CopyDestination, {}});
			for (std::uint32_t mip = 0u; mip < mip_count; ++mip) {
				region.mip_level = mip;
				region.width = (std::max)(width >> mip, 1u);
				region.height = (std::max)(height >> mip, 1u);
				layout.offset = offset + mip * slice_size;
				read.Record(CopyTextureToBuffer{clone, destination, region, layout});
			}
			std::promise<CommandGraphResources> result;
			auto future = result.get_future();
			auto operation = std::move(graph).connect(Receiver{&result});
			operation.BindResource(source, std::move(upload));
			operation.BindResource(image, std::move(texture));
			operation.BindResource(clone, std::move(cloned));
			operation.BindResource(destination, std::move(readback));
			operation.start();
			auto resources = future.get();
			readback = resources.TakeResource(destination);
			auto mapped = readback.Map({0u, size});
			for (std::uint32_t mip = 0u; mip < mip_count; ++mip) {
				auto mip_rows = ((std::max)(height >> mip, 1u) + 3u) / 4u;
				auto mip_pitch = ((std::max)(width >> mip, 1u) + 3u) / 4u * block_bytes;
				for (std::uint32_t layer = 0u; layer < layers; ++layer) {
					for (std::uint32_t row = 0u; row < mip_rows; ++row) {
						auto start = offset + (layer * rows + row) * pitch;
						if (!std::ranges::equal(mapped.Read().subspan(start + mip * slice_size, mip_pitch),
							std::span(bytes).subspan(start, mip_pitch))) {
							throw std::runtime_error("BC block readback mismatch");
						}
					}
				}
			}
			Resource software_texture;
			if (compare && backend == Backend::WebGPU) {
				software_texture = UploadComparisonTexture(
					&software_device,
					&software_scheduler,
					software_device.CreateTexture(width, height, layers, mip_count, flags),
					mapped.Read(), offset, pitch, rows, slice_size
				);
			}
			mapped.Reset();
			if (sample && backend == Backend::WebGPU) {
				std::cout << "Sampling BC format " << static_cast<unsigned>(format)
					<< " at " << width << 'x' << height << '\n';
				if (compare) {
					std::vector<std::byte> native_pixels;
					std::vector<std::byte> software_pixels;
					auto native_texture = resources.TakeResource(clone);
					bool anisotropic = mode.ends_with(".aniso");
					bool floating = channels || bptc || mode.ends_with(".float") || anisotropic;
					for (std::uint32_t layer = 0u; layer < (bptc ? layers : 1u); ++layer) {
						for (std::uint32_t mip = 0u; mip < (bptc ? mip_count : 1u); ++mip) {
							auto sample_width = (std::max)(width >> mip, 1u);
							auto sample_height = (std::max)(height >> mip, 1u);
							for (bool linear : {false, true}) {
								if (anisotropic && !linear) {
									continue;
								}
								for (bool srgb_target : {false, true}) {
									if (floating && srgb_target) {
										continue;
									}
									native_texture = CheckSample(
										&device, &scheduler, std::move(native_texture), flags,
										sample_width, sample_height, srgb_target, {}, &native_pixels,
										linear, floating, mip, layer, layers > 1u, anisotropic
									);
									software_texture = CheckSample(
										&software_device, &software_scheduler, std::move(software_texture), flags,
										sample_width, sample_height, srgb_target, {}, &software_pixels,
										linear, floating, mip, layer, layers > 1u, anisotropic
									);
									if (!std::ranges::equal(native_pixels, software_pixels)) {
										auto mismatch = std::ranges::mismatch(native_pixels, software_pixels);
										auto byte = static_cast<std::size_t>(mismatch.in1 - native_pixels.begin());
										if (floating) {
											std::uint32_t native_bits;
											std::uint32_t software_bits;
											std::memcpy(&native_bits, native_pixels.data() + byte / 4u * 4u, 4u);
											std::memcpy(&software_bits, software_pixels.data() + byte / 4u * 4u, 4u);
											std::cerr << "Float bits " << std::hex << native_bits << '/' << software_bits << std::dec << '\n';
										}
										auto pixel_bytes = floating ? 16u : 4u;
										std::cerr << "Pixel " << byte / pixel_bytes % sample_width << ',' << byte / pixel_bytes / sample_width
											<< " channel " << (floating ? byte % pixel_bytes / 4u : byte % pixel_bytes)
											<< " native byte " << std::to_integer<unsigned>(*mismatch.in1)
											<< " software " << std::to_integer<unsigned>(*mismatch.in2)
											<< " mip " << mip << " layer " << layer
											<< " linear " << linear << " sRGB target " << srgb_target << '\n';
										throw std::runtime_error("Native/software BC sampling is not byte-identical");
									}
								}
							}
						}
					}
				}
				else {
					if (bptc) {
						std::vector<std::byte> pixels;
						(void)CheckSample(
							&device, &scheduler, resources.TakeResource(clone), flags, width, height,
							false, {}, &pixels, false, true
						);
						for (std::uint32_t y = 0u; y < height; ++y) {
							for (std::uint32_t x = 0u; x < width; ++x) {
								bool hdr = format == Bits::Bc6HUfloat || format == Bits::Bc6HSfloat;
								auto encoded = ((y / 4u) * block_columns + x / 4u) % 9u;
								std::array reference{0.0f, 0.0f, 0.0f, hdr || encoded < 4u ? 1.0f : 0.0f};
								if (std::memcmp(
									reference.data(),
									pixels.data() + (static_cast<std::size_t>(y) * width + x) * 16u,
									16u
								) != 0) {
									throw std::runtime_error("Feature-absent BPTC zero block differs from specified output");
								}
							}
						}
					}
					else if (channels) {
						std::vector<std::byte> pixels;
						(void)CheckSample(
							&device, &scheduler, resources.TakeResource(clone), flags, width, height,
							false, {}, &pixels, false, true
						);
						bool signed_channels = format == Bits::Bc4Snorm || format == Bits::Bc5Snorm;
						/// @brief CPU double division rounds the exact normalized16 integer to float32.
						auto DecodeChannel = [signed_channels, intel](unsigned first, unsigned second, unsigned index) {
							auto Convert = [signed_channels](unsigned value) {
								return signed_channels ? (std::max)(static_cast<int>(value) - (value >= 128u ? 256 : 0), -127) : static_cast<int>(value);
							};
							auto Expand = [signed_channels](int value) {
								if (!signed_channels) {
									return value * 257;
								}
								auto magnitude = ((value < 0 ? -value : value) * 16513) >> 6;
								return value < 0 ? -magnitude : magnitude;
							};
							auto a = Convert(first);
							auto b = Convert(second);
							if (a <= b && index >= 6u) {
								return index == 7u ? 1.0f : signed_channels ? -1.0f : 0.0f;
							}
							if (intel) {
								auto denominator = a > b ? 7u : 5u;
								auto weight = index == 0u ? 0u : index == 1u ? 65536u :
									((index - 1u) * 65536u + denominator / 2u) / denominator;
								auto value = ((65536 - static_cast<int>(weight)) * a + static_cast<int>(weight) * b + 128) >> 8;
								if (signed_channels) {
									auto magnitude = value < 0 ? -value : value;
									auto expanded = magnitude + (magnitude >> 7) + (magnitude >> 14);
									value = value < 0 ? -expanded : expanded;
								}
								else {
									value += value >> 8;
								}
								return static_cast<float>(static_cast<double>(value) / (signed_channels ? 32767.0 : 65535.0));
							}
							int result = index == 1u ? Expand(b) : Expand(a);
							if (index >= 2u) {
								constexpr int seven[]{1, 2, -4, 4, -2, -1};
								constexpr int five[]{1, 2, -2, -1};
								auto factor = a > b ? seven[index - 2u] : five[index - 2u];
								result = Expand(factor < 0 ? b : a) + (a > b ? 36 : 48) * (b - a) * factor;
							}
							return static_cast<float>(static_cast<double>(result) / (signed_channels ? 32767.0 : 65535.0));
						};
						for (std::uint32_t y = 0u; y < height; ++y) {
							for (std::uint32_t x = 0u; x < width; ++x) {
								auto index = ((y % 4u) * 4u + x % 4u) % 8u;
								std::array reference{
									DecodeChannel(x / 4u, y / 4u, index),
									block_bytes == 16u ? DecodeChannel(y / 4u, x / 4u, index) : 0.0f, 0.0f, 1.0f
								};
								if (std::memcmp(
									reference.data(),
									pixels.data() + (static_cast<std::size_t>(y) * width + x) * 16u,
									16u
								) != 0) {
									throw std::runtime_error("Feature-absent BC4/BC5 differs from normalized16 reference");
								}
							}
						}
					}
					else {
						(void)CheckSample(&device, &scheduler, resources.TakeResource(clone), flags, width, height, false, expected);
					}
				}
			}
		}
	}
	if (sink.failed.load()) {
		throw std::runtime_error("BC copy emitted native validation errors");
	}
	std::cout << name << " BC round trips passed\n";
	return 0;
} catch (std::exception const& error) {
	std::cerr << error.what() << '\n';
	return 1;
}
