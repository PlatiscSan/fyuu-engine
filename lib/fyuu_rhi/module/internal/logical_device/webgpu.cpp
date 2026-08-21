module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <vector>

#include <algorithm>
#include <iterator>

#include <cstdint>

#include <optional>

#include <ranges>
#endif // !defined(__cpp_lib_modules)
#include <dawn/webgpu_cpp.h>
#include <slang.h>

module fyuu_rhi:webgpu_logical_device;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :logical_device_dispatch;
import :pipeline;
import :pipeline_factory;
import :resource_factory;
import :sampler;
import :sampler_factory;
import :slang;
import :webgpu_data;
import :webgpu_utility;

namespace {

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::pipeline;

	wgpu::CompareFunction MapCompareOperation(CompareOperation operation) noexcept {
		switch (operation) {
		case CompareOperation::Never: return wgpu::CompareFunction::Never;
		case CompareOperation::Less: return wgpu::CompareFunction::Less;
		case CompareOperation::Equal: return wgpu::CompareFunction::Equal;
		case CompareOperation::LessEqual: return wgpu::CompareFunction::LessEqual;
		case CompareOperation::Greater: return wgpu::CompareFunction::Greater;
		case CompareOperation::NotEqual: return wgpu::CompareFunction::NotEqual;
		case CompareOperation::GreaterEqual: return wgpu::CompareFunction::GreaterEqual;
		case CompareOperation::Always: return wgpu::CompareFunction::Always;
		default: return wgpu::CompareFunction::Always;
		}
	}

	wgpu::StencilOperation MapStencilOperation(pipeline::StencilOperation operation) noexcept {
		switch (operation) {
		case pipeline::StencilOperation::Keep: return wgpu::StencilOperation::Keep;
		case pipeline::StencilOperation::Zero: return wgpu::StencilOperation::Zero;
		case pipeline::StencilOperation::Replace: return wgpu::StencilOperation::Replace;
		case pipeline::StencilOperation::Invert: return wgpu::StencilOperation::Invert;
		case pipeline::StencilOperation::IncrementClamp: return wgpu::StencilOperation::IncrementClamp;
		case pipeline::StencilOperation::DecrementClamp: return wgpu::StencilOperation::DecrementClamp;
		case pipeline::StencilOperation::IncrementWrap: return wgpu::StencilOperation::IncrementWrap;
		case pipeline::StencilOperation::DecrementWrap: return wgpu::StencilOperation::DecrementWrap;
		default: return wgpu::StencilOperation::Keep;
		}
	}

	wgpu::StencilFaceState MapStencilFace(StencilFaceState const& state) noexcept {
		return {
			.compare = MapCompareOperation(state.compare),
			.failOp = MapStencilOperation(state.fail_operation),
			.depthFailOp = MapStencilOperation(state.depth_fail_operation),
			.passOp = MapStencilOperation(state.pass_operation)
		};
	}

	wgpu::BlendFactor MapBlendFactor(pipeline::BlendFactor factor) noexcept {
		switch (factor) {
		case pipeline::BlendFactor::Zero: return wgpu::BlendFactor::Zero;
		case pipeline::BlendFactor::One: return wgpu::BlendFactor::One;
		case pipeline::BlendFactor::SourceColor: return wgpu::BlendFactor::Src;
		case pipeline::BlendFactor::OneMinusSourceColor: return wgpu::BlendFactor::OneMinusSrc;
		case pipeline::BlendFactor::SourceAlpha: return wgpu::BlendFactor::SrcAlpha;
		case pipeline::BlendFactor::OneMinusSourceAlpha: return wgpu::BlendFactor::OneMinusSrcAlpha;
		case pipeline::BlendFactor::DestinationColor: return wgpu::BlendFactor::Dst;
		case pipeline::BlendFactor::OneMinusDestinationColor: return wgpu::BlendFactor::OneMinusDst;
		case pipeline::BlendFactor::DestinationAlpha: return wgpu::BlendFactor::DstAlpha;
		case pipeline::BlendFactor::OneMinusDestinationAlpha: return wgpu::BlendFactor::OneMinusDstAlpha;
		case pipeline::BlendFactor::SourceAlphaSaturated: return wgpu::BlendFactor::SrcAlphaSaturated;
		case pipeline::BlendFactor::Constant: return wgpu::BlendFactor::Constant;
		case pipeline::BlendFactor::OneMinusConstant: return wgpu::BlendFactor::OneMinusConstant;
		default: return wgpu::BlendFactor::One;
		}
	}

	wgpu::BlendOperation MapBlendOperation(pipeline::BlendOperation operation) noexcept {
		switch (operation) {
		case pipeline::BlendOperation::Add: return wgpu::BlendOperation::Add;
		case pipeline::BlendOperation::Subtract: return wgpu::BlendOperation::Subtract;
		case pipeline::BlendOperation::ReverseSubtract: return wgpu::BlendOperation::ReverseSubtract;
		case pipeline::BlendOperation::Min: return wgpu::BlendOperation::Min;
		case pipeline::BlendOperation::Max: return wgpu::BlendOperation::Max;
		default: return wgpu::BlendOperation::Add;
		}
	}

	wgpu::VertexFormat MapVertexFormat(ResourceFlagBits format) {
		switch (format) {
		case ResourceFlagBits::R8Uint: return wgpu::VertexFormat::Uint8;
		case ResourceFlagBits::R8Sint: return wgpu::VertexFormat::Sint8;
		case ResourceFlagBits::R8G8Uint: return wgpu::VertexFormat::Uint8x2;
		case ResourceFlagBits::R8G8Sint: return wgpu::VertexFormat::Sint8x2;
		case ResourceFlagBits::R8G8B8A8Unorm: return wgpu::VertexFormat::Unorm8x4;
		case ResourceFlagBits::R8G8B8A8Snorm: return wgpu::VertexFormat::Snorm8x4;
		case ResourceFlagBits::R8G8B8A8Uint: return wgpu::VertexFormat::Uint8x4;
		case ResourceFlagBits::R8G8B8A8Sint: return wgpu::VertexFormat::Sint8x4;
		case ResourceFlagBits::R16G16Uint: return wgpu::VertexFormat::Uint16x2;
		case ResourceFlagBits::R16G16Sint: return wgpu::VertexFormat::Sint16x2;
		case ResourceFlagBits::R16G16Float: return wgpu::VertexFormat::Float16x2;
		case ResourceFlagBits::R16G16B16A16Uint: return wgpu::VertexFormat::Uint16x4;
		case ResourceFlagBits::R16G16B16A16Sint: return wgpu::VertexFormat::Sint16x4;
		case ResourceFlagBits::R16G16B16A16Float: return wgpu::VertexFormat::Float16x4;
		case ResourceFlagBits::R32Uint: return wgpu::VertexFormat::Uint32;
		case ResourceFlagBits::R32Sint: return wgpu::VertexFormat::Sint32;
		case ResourceFlagBits::R32Float: return wgpu::VertexFormat::Float32;
		case ResourceFlagBits::R32G32Uint: return wgpu::VertexFormat::Uint32x2;
		case ResourceFlagBits::R32G32Sint: return wgpu::VertexFormat::Sint32x2;
		case ResourceFlagBits::R32G32Float: return wgpu::VertexFormat::Float32x2;
		case ResourceFlagBits::R32G32B32A32Uint: return wgpu::VertexFormat::Uint32x4;
		case ResourceFlagBits::R32G32B32A32Sint: return wgpu::VertexFormat::Sint32x4;
		case ResourceFlagBits::R32G32B32A32Float: return wgpu::VertexFormat::Float32x4;
		default: throw std::invalid_argument("Unsupported WebGPU vertex format");
		}
	}

	wgpu::PrimitiveTopology MapPrimitiveTopology(pipeline::PrimitiveTopology topology) noexcept {
		switch (topology) {
		case pipeline::PrimitiveTopology::PointList: return wgpu::PrimitiveTopology::PointList;
		case pipeline::PrimitiveTopology::LineList: return wgpu::PrimitiveTopology::LineList;
		case pipeline::PrimitiveTopology::LineStrip: return wgpu::PrimitiveTopology::LineStrip;
		case pipeline::PrimitiveTopology::TriangleList: return wgpu::PrimitiveTopology::TriangleList;
		case pipeline::PrimitiveTopology::TriangleStrip: return wgpu::PrimitiveTopology::TriangleStrip;
		default: return wgpu::PrimitiveTopology::TriangleList;
		}
	}

	wgpu::IndexFormat MapIndexFormat(std::optional<pipeline::IndexFormat> format) noexcept {
		if (!format) {
			return wgpu::IndexFormat::Undefined;
		}
		if (*format == pipeline::IndexFormat::Uint16) {
			return wgpu::IndexFormat::Uint16;
		}
		return wgpu::IndexFormat::Uint32;
	}

	wgpu::VertexStepMode MapVertexStepMode(VertexInputRate input_rate) noexcept {
		if (input_rate == VertexInputRate::Vertex) {
			return wgpu::VertexStepMode::Vertex;
		}
		return wgpu::VertexStepMode::Instance;
	}

	wgpu::FrontFace MapFrontFace(pipeline::FrontFace front_face) noexcept {
		if (front_face == pipeline::FrontFace::CounterClockwise) {
			return wgpu::FrontFace::CCW;
		}
		return wgpu::FrontFace::CW;
	}

	wgpu::CullMode MapCullMode(pipeline::CullMode cull_mode) noexcept {
		if (cull_mode == pipeline::CullMode::Front) {
			return wgpu::CullMode::Front;
		}
		if (cull_mode == pipeline::CullMode::Back) {
			return wgpu::CullMode::Back;
		}
		return wgpu::CullMode::None;
	}

	std::uint32_t BindGroupCount(shader::SlangPipelineInterface const& interface) noexcept {
		std::uint32_t result = 0u;
		std::ranges::for_each(
			interface.bindings,
			[&](auto const& binding) {
				result = (std::max)(result, binding.space + 1u);
			}
		);
		return result;
	}

} // namespace

namespace fyuu_rhi {

	template <>
	struct CreateBuffer<webgpu::LogicalDevice> {
		webgpu::LogicalDevice* logical_device;

		Resource operator()(std::size_t size_in_bytes, ResourceFlags const& flags) const {
			wgpu::BufferDescriptor descriptor{
				nullptr,
				{},
				webgpu::BufferUsage(flags),
				size_in_bytes,
				false
			};
			return MakeResource(
				webgpu::Resource{ logical_device->impl.CreateBuffer(&descriptor) },
				size_in_bytes,
				flags
			);
		}
	};

	template <>
	struct CreateTexture<webgpu::LogicalDevice> {
		webgpu::LogicalDevice* logical_device;

		Resource operator()(
			std::size_t width,
			std::size_t height,
			std::size_t depth_or_array_layers,
			std::size_t mip_levels,
			ResourceFlags const& flags
		) const {
			wgpu::TextureDescriptor descriptor{
				nullptr,
				{},
				webgpu::TextureUsage(flags),
				webgpu::TextureDimension(flags),
				{
					static_cast<std::uint32_t>(width),
					static_cast<std::uint32_t>(height),
					static_cast<std::uint32_t>(depth_or_array_layers)
				},
				webgpu::ResourceFormat(flags),
				static_cast<std::uint32_t>(mip_levels),
				webgpu::SampleCount(flags),
				0,
				nullptr
			};
			return MakeResource(
				webgpu::Resource{ logical_device->impl.CreateTexture(&descriptor) },
				ResourceTextureExtent{
					.width = static_cast<std::uint32_t>(width),
					.height = static_cast<std::uint32_t>(height),
					.depth_or_array_layers = static_cast<std::uint32_t>(depth_or_array_layers),
					.mip_levels = static_cast<std::uint32_t>(mip_levels)
				},
				flags
			);
		}
	};

	template <>
	struct CreateSampler<webgpu::LogicalDevice> {
		webgpu::LogicalDevice* logical_device;

		Sampler operator()(SamplerDescriptor const& descriptor) const {
			wgpu::SamplerDescriptor native_descriptor = {
				.addressModeU = webgpu::SamplerAddressMode(descriptor.address_mode_u),
				.addressModeV = webgpu::SamplerAddressMode(descriptor.address_mode_v),
				.addressModeW = webgpu::SamplerAddressMode(descriptor.address_mode_w),
				.magFilter = webgpu::Filter(descriptor.mag_filter),
				.minFilter = webgpu::Filter(descriptor.min_filter),
				.mipmapFilter = webgpu::MipmapFilter(descriptor.mipmap_filter),
				.lodMinClamp = descriptor.min_lod,
				.lodMaxClamp = descriptor.max_lod,
				.compare = webgpu::ComparisonFunction(descriptor.compare_function),
				.maxAnisotropy = descriptor.max_anisotropy
			};
			return MakeSampler(webgpu::Sampler{ logical_device->impl.CreateSampler(&native_descriptor) });
		}
	};

	template <>
	struct CreateGraphicsPipeline<webgpu::LogicalDevice> {
		webgpu::LogicalDevice* logical_device;

		Pipeline operator()(pipeline::GraphicsPipelineDescriptor const& descriptor) const {
			slang::TargetDesc target{ .format = SLANG_WGSL };
			shader::SlangProgram program(
				target,
				descriptor.program,
				"webgpu-wgsl"
			);
			if (!program.GetInterface().push_constants.empty()) {
				throw std::invalid_argument("WebGPU does not support push constants");
			}

			std::vector<wgpu::ShaderModule> modules;
			modules.reserve(program.GetEntryPoints().size());
			wgpu::ShaderModule vertex_module;
			wgpu::ShaderModule fragment_module;
			wgpu::StringView vertex_entry;
			wgpu::StringView fragment_entry;
			std::ranges::for_each(
				program.GetEntryPoints(),
				[&](auto const& entry) {
					if (
						entry.stage != pipeline::Stage::Vertex &&
						entry.stage != pipeline::Stage::Fragment
					) {
						throw std::invalid_argument(
							"WebGPU graphics pipelines support only vertex and fragment stages"
						);
					}
					wgpu::ShaderSourceWGSL source(
						wgpu::ShaderSourceWGSL::Init{
							nullptr,
							{
							reinterpret_cast<char const*>(entry.code.data()),
							entry.code.size()
							}
						}
					);
					wgpu::ShaderModuleDescriptor module_descriptor{
						.nextInChain = &source
					};
					auto module = logical_device->impl.CreateShaderModule(
						&module_descriptor
					);
					modules.emplace_back(module);
					if (entry.stage == pipeline::Stage::Vertex) {
						if (vertex_module) {
							throw std::invalid_argument(
								"A WebGPU graphics pipeline cannot contain duplicate vertex stages"
							);
						}
						vertex_module = module;
						vertex_entry = { entry.name.data(), entry.name.size() };
					}
					else {
						if (fragment_module) {
							throw std::invalid_argument(
								"A WebGPU graphics pipeline cannot contain duplicate fragment stages"
							);
						}
						fragment_module = module;
						fragment_entry = { entry.name.data(), entry.name.size() };
					}
				}
			);
			if (!vertex_module) {
				throw std::invalid_argument(
					"A WebGPU graphics pipeline requires one vertex entry point"
				);
			}

			std::vector<std::vector<wgpu::VertexAttribute>> attributes(
				descriptor.vertex.buffers.size()
			);
			std::vector<wgpu::VertexBufferLayout> buffers;
			buffers.reserve(descriptor.vertex.buffers.size());
			std::ranges::transform(
				descriptor.vertex.buffers,
				std::back_inserter(buffers),
				[&, index = std::size_t{ 0u }](auto const& buffer) mutable {
					auto& buffer_attributes = attributes[index++];
					std::ranges::transform(
						descriptor.vertex.attributes |
							std::views::filter(
								[slot = buffer.slot](auto const& attribute) {
									return attribute.slot == slot;
								}
							),
						std::back_inserter(buffer_attributes),
						[](auto const& attribute) {
							return wgpu::VertexAttribute{
								.format = MapVertexFormat(attribute.format),
								.offset = attribute.offset,
								.shaderLocation = attribute.location
							};
						}
					);
					return wgpu::VertexBufferLayout{
						.stepMode = MapVertexStepMode(buffer.input_rate),
						.arrayStride = buffer.stride,
						.attributeCount = buffer_attributes.size(),
						.attributes = buffer_attributes.data()
					};
				}
			);

			wgpu::VertexState vertex{
				.module = vertex_module,
				.entryPoint = vertex_entry,
				.bufferCount = buffers.size(),
				.buffers = buffers.data()
			};
			wgpu::PrimitiveState primitive{
				.topology = MapPrimitiveTopology(descriptor.primitive.topology),
				.stripIndexFormat = MapIndexFormat(descriptor.primitive.strip_index_format),
				.frontFace = MapFrontFace(descriptor.rasterization.front_face),
				.cullMode = MapCullMode(descriptor.rasterization.cull_mode)
			};

			std::optional<wgpu::DepthStencilState> depth_stencil;
			if (descriptor.depth_stencil) {
				auto depth_write_enabled = wgpu::OptionalBool::False;
				if (descriptor.depth_stencil->depth_write_enabled) {
					depth_write_enabled = wgpu::OptionalBool::True;
				}
				auto depth_compare = wgpu::CompareFunction::Always;
				if (descriptor.depth_stencil->depth_test_enabled) {
					depth_compare = MapCompareOperation(
						descriptor.depth_stencil->depth_compare
					);
				}
				depth_stencil = wgpu::DepthStencilState{
					.format = webgpu::ResourceFormat(
						ResourceFlags(descriptor.depth_stencil->format)
					),
					.depthWriteEnabled = depth_write_enabled,
					.depthCompare = depth_compare,
					.stencilFront = MapStencilFace(descriptor.depth_stencil->stencil_front),
					.stencilBack = MapStencilFace(descriptor.depth_stencil->stencil_back),
					.stencilReadMask = descriptor.depth_stencil->stencil_read_mask,
					.stencilWriteMask = descriptor.depth_stencil->stencil_write_mask,
					.depthBias = descriptor.rasterization.depth_bias.constant,
					.depthBiasSlopeScale = descriptor.rasterization.depth_bias.slope_scale,
					.depthBiasClamp = descriptor.rasterization.depth_bias.clamp
				};
			}

			std::vector<wgpu::BlendState> blends;
			std::vector<wgpu::ColorTargetState> color_targets;
			blends.reserve(descriptor.color_targets.size());
			color_targets.reserve(descriptor.color_targets.size());
			std::ranges::for_each(
				descriptor.color_targets,
				[&](auto const& target_state) {
					wgpu::BlendState* blend = nullptr;
					if (target_state.blend) {
						blends.emplace_back(
							wgpu::BlendState{
								.color = {
									.operation = MapBlendOperation(target_state.blend->color.operation),
									.srcFactor = MapBlendFactor(target_state.blend->color.source_factor),
									.dstFactor = MapBlendFactor(target_state.blend->color.destination_factor)
								},
								.alpha = {
									.operation = MapBlendOperation(target_state.blend->alpha.operation),
									.srcFactor = MapBlendFactor(target_state.blend->alpha.source_factor),
									.dstFactor = MapBlendFactor(target_state.blend->alpha.destination_factor)
								}
							}
						);
						blend = &blends.back();
					}
					color_targets.emplace_back(
						wgpu::ColorTargetState{
							.format = webgpu::ResourceFormat(ResourceFlags(target_state.format)),
							.blend = blend,
							.writeMask = static_cast<wgpu::ColorWriteMask>(
								static_cast<std::uint8_t>(target_state.write_mask)
							)
						}
					);
				}
			);

			std::optional<wgpu::FragmentState> fragment;
			if (fragment_module) {
				fragment = wgpu::FragmentState{
					.module = fragment_module,
					.entryPoint = fragment_entry,
					.targetCount = color_targets.size(),
					.targets = color_targets.data()
				};
			}
			wgpu::RenderPipelineDescriptor pipeline_descriptor{
				.layout = nullptr,
				.vertex = vertex,
				.primitive = primitive,
				.depthStencil = depth_stencil ? &*depth_stencil : nullptr,
				.multisample = {
					.count = webgpu::SampleCount(
						ResourceFlags(descriptor.multisample.sample_count)
					),
					.mask = descriptor.multisample.mask,
					.alphaToCoverageEnabled = descriptor.multisample.alpha_to_coverage_enabled
				},
				.fragment = fragment ? &*fragment : nullptr
			};

			auto native_pipeline = logical_device->impl.CreateRenderPipeline(
				&pipeline_descriptor
			);
			std::vector<wgpu::BindGroupLayout> bind_group_layouts;
			bind_group_layouts.reserve(BindGroupCount(program.GetInterface()));
			std::ranges::transform(
				std::views::iota(0u, BindGroupCount(program.GetInterface())),
				std::back_inserter(bind_group_layouts),
				[&](std::uint32_t index) {
					return native_pipeline.GetBindGroupLayout(index);
				}
			);
			return MakePipeline(
				webgpu::Pipeline{
					logical_device->impl,
					std::move(bind_group_layouts),
					pipeline::MakePipelineBindingMetadata(program.GetInterface()),
					std::move(native_pipeline)
				}
			);
		}
	};

	template <>
	struct CreateComputePipeline<webgpu::LogicalDevice> {
		webgpu::LogicalDevice* logical_device;

		Pipeline operator()(pipeline::ComputePipelineDescriptor const& descriptor) const {
			slang::TargetDesc target{ .format = SLANG_WGSL };
			shader::SlangProgram program(
				target,
				descriptor.program,
				"webgpu-wgsl"
			);
			if (!program.GetInterface().push_constants.empty()) {
				throw std::invalid_argument("WebGPU does not support push constants");
			}
			if (
				program.GetEntryPoints().size() != 1u ||
				program.GetEntryPoints().front().stage != pipeline::Stage::Compute
			) {
				throw std::invalid_argument(
					"A WebGPU compute pipeline requires exactly one compute entry point"
				);
			}

			auto const& entry = program.GetEntryPoints().front();
			wgpu::ShaderSourceWGSL source(
				wgpu::ShaderSourceWGSL::Init{
					nullptr,
					{
					reinterpret_cast<char const*>(entry.code.data()),
					entry.code.size()
					}
				}
			);
			wgpu::ShaderModuleDescriptor module_descriptor{
				.nextInChain = &source
			};
			auto module = logical_device->impl.CreateShaderModule(&module_descriptor);
			wgpu::ComputePipelineDescriptor pipeline_descriptor{
				.compute = {
					.module = module,
					.entryPoint = { entry.name.data(), entry.name.size() }
				}
			};
			auto native_pipeline = logical_device->impl.CreateComputePipeline(
				&pipeline_descriptor
			);
			std::vector<wgpu::BindGroupLayout> bind_group_layouts;
			bind_group_layouts.reserve(BindGroupCount(program.GetInterface()));
			std::ranges::transform(
				std::views::iota(0u, BindGroupCount(program.GetInterface())),
				std::back_inserter(bind_group_layouts),
				[&](std::uint32_t index) {
					return native_pipeline.GetBindGroupLayout(index);
				}
			);
			return MakePipeline(
				webgpu::Pipeline{
					logical_device->impl,
					std::move(bind_group_layouts),
					pipeline::MakePipelineBindingMetadata(program.GetInterface()),
					std::move(native_pipeline)
				}
			);
		}
	};

} // namespace fyuu_rhi
