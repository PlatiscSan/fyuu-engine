module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <algorithm>
#include <iterator>

#include <cstdint>

#include <optional>

#include <ranges>
#endif // !defined(__cpp_lib_modules)
#if defined(__APPLE__)
#include <Metal/Metal.hpp>
#include <slang.h>
#endif // defined(__APPLE__)

module fyuu_rhi:metal_logical_device;
#if defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :logical_device_dispatch;
import :metal_data;
import :metal_utility;
import :pipeline;
import :pipeline_factory;
import :resource_factory;
import :sampler;
import :sampler_factory;
import :slang;

namespace {

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::pipeline;

	std::string ErrorMessage(NS::Error const* error) {
		if (!error || !error->localizedDescription()) {
			return "Metal returned no diagnostic";
		}
		auto message = error->localizedDescription()->utf8String();
		return message ? message : "Metal returned an invalid diagnostic";
	}

	MTL::VertexFormat MapVertexFormat(ResourceFlagBits format) {
		switch (format) {
		case ResourceFlagBits::R8Uint: return MTL::VertexFormatUChar;
		case ResourceFlagBits::R8Sint: return MTL::VertexFormatChar;
		case ResourceFlagBits::R8G8Uint: return MTL::VertexFormatUChar2;
		case ResourceFlagBits::R8G8Sint: return MTL::VertexFormatChar2;
		case ResourceFlagBits::R8G8B8A8Unorm: return MTL::VertexFormatUChar4Normalized;
		case ResourceFlagBits::R8G8B8A8Snorm: return MTL::VertexFormatChar4Normalized;
		case ResourceFlagBits::R8G8B8A8Uint: return MTL::VertexFormatUChar4;
		case ResourceFlagBits::R8G8B8A8Sint: return MTL::VertexFormatChar4;
		case ResourceFlagBits::R16G16Uint: return MTL::VertexFormatUShort2;
		case ResourceFlagBits::R16G16Sint: return MTL::VertexFormatShort2;
		case ResourceFlagBits::R16G16Float: return MTL::VertexFormatHalf2;
		case ResourceFlagBits::R16G16B16A16Uint: return MTL::VertexFormatUShort4;
		case ResourceFlagBits::R16G16B16A16Sint: return MTL::VertexFormatShort4;
		case ResourceFlagBits::R16G16B16A16Float: return MTL::VertexFormatHalf4;
		case ResourceFlagBits::R32Uint: return MTL::VertexFormatUInt;
		case ResourceFlagBits::R32Sint: return MTL::VertexFormatInt;
		case ResourceFlagBits::R32Float: return MTL::VertexFormatFloat;
		case ResourceFlagBits::R32G32Uint: return MTL::VertexFormatUInt2;
		case ResourceFlagBits::R32G32Sint: return MTL::VertexFormatInt2;
		case ResourceFlagBits::R32G32Float: return MTL::VertexFormatFloat2;
		case ResourceFlagBits::R32G32B32A32Uint: return MTL::VertexFormatUInt4;
		case ResourceFlagBits::R32G32B32A32Sint: return MTL::VertexFormatInt4;
		case ResourceFlagBits::R32G32B32A32Float: return MTL::VertexFormatFloat4;
		default: throw std::invalid_argument("Unsupported Metal vertex format");
		}
	}

	MTL::PrimitiveType MapPrimitiveType(PrimitiveTopology topology) noexcept {
		switch (topology) {
		case PrimitiveTopology::PointList: return MTL::PrimitiveTypePoint;
		case PrimitiveTopology::LineList: return MTL::PrimitiveTypeLine;
		case PrimitiveTopology::LineStrip: return MTL::PrimitiveTypeLineStrip;
		case PrimitiveTopology::TriangleList: return MTL::PrimitiveTypeTriangle;
		case PrimitiveTopology::TriangleStrip: return MTL::PrimitiveTypeTriangleStrip;
		default: return MTL::PrimitiveTypeTriangle;
		}
	}

	MTL::PrimitiveTopologyClass MapTopologyClass(PrimitiveTopology topology) noexcept {
		switch (topology) {
		case PrimitiveTopology::PointList: return MTL::PrimitiveTopologyClassPoint;
		case PrimitiveTopology::LineList:
		case PrimitiveTopology::LineStrip: return MTL::PrimitiveTopologyClassLine;
		case PrimitiveTopology::TriangleList:
		case PrimitiveTopology::TriangleStrip: return MTL::PrimitiveTopologyClassTriangle;
		default: return MTL::PrimitiveTopologyClassUnspecified;
		}
	}

	MTL::CompareFunction MapCompareOperation(CompareOperation operation) noexcept {
		switch (operation) {
		case CompareOperation::Never: return MTL::CompareFunctionNever;
		case CompareOperation::Less: return MTL::CompareFunctionLess;
		case CompareOperation::Equal: return MTL::CompareFunctionEqual;
		case CompareOperation::LessEqual: return MTL::CompareFunctionLessEqual;
		case CompareOperation::Greater: return MTL::CompareFunctionGreater;
		case CompareOperation::NotEqual: return MTL::CompareFunctionNotEqual;
		case CompareOperation::GreaterEqual: return MTL::CompareFunctionGreaterEqual;
		case CompareOperation::Always: return MTL::CompareFunctionAlways;
		default: return MTL::CompareFunctionAlways;
		}
	}

	MTL::StencilOperation MapStencilOperation(pipeline::StencilOperation operation) noexcept {
		switch (operation) {
		case pipeline::StencilOperation::Keep: return MTL::StencilOperationKeep;
		case pipeline::StencilOperation::Zero: return MTL::StencilOperationZero;
		case pipeline::StencilOperation::Replace: return MTL::StencilOperationReplace;
		case pipeline::StencilOperation::Invert: return MTL::StencilOperationInvert;
		case pipeline::StencilOperation::IncrementClamp: return MTL::StencilOperationIncrementClamp;
		case pipeline::StencilOperation::DecrementClamp: return MTL::StencilOperationDecrementClamp;
		case pipeline::StencilOperation::IncrementWrap: return MTL::StencilOperationIncrementWrap;
		case pipeline::StencilOperation::DecrementWrap: return MTL::StencilOperationDecrementWrap;
		default: return MTL::StencilOperationKeep;
		}
	}

	NS::SharedPtr<MTL::StencilDescriptor> MakeStencilDescriptor(
		StencilFaceState const& state,
		std::uint32_t read_mask,
		std::uint32_t write_mask
	) {
		auto result = NS::TransferPtr(MTL::StencilDescriptor::alloc()->init());
		result->setStencilCompareFunction(MapCompareOperation(state.compare));
		result->setStencilFailureOperation(MapStencilOperation(state.fail_operation));
		result->setDepthFailureOperation(MapStencilOperation(state.depth_fail_operation));
		result->setDepthStencilPassOperation(MapStencilOperation(state.pass_operation));
		result->setReadMask(read_mask);
		result->setWriteMask(write_mask);
		return result;
	}

	MTL::BlendFactor MapBlendFactor(pipeline::BlendFactor factor) noexcept {
		switch (factor) {
		case pipeline::BlendFactor::Zero: return MTL::BlendFactorZero;
		case pipeline::BlendFactor::One: return MTL::BlendFactorOne;
		case pipeline::BlendFactor::SourceColor: return MTL::BlendFactorSourceColor;
		case pipeline::BlendFactor::OneMinusSourceColor: return MTL::BlendFactorOneMinusSourceColor;
		case pipeline::BlendFactor::SourceAlpha: return MTL::BlendFactorSourceAlpha;
		case pipeline::BlendFactor::OneMinusSourceAlpha: return MTL::BlendFactorOneMinusSourceAlpha;
		case pipeline::BlendFactor::DestinationColor: return MTL::BlendFactorDestinationColor;
		case pipeline::BlendFactor::OneMinusDestinationColor: return MTL::BlendFactorOneMinusDestinationColor;
		case pipeline::BlendFactor::DestinationAlpha: return MTL::BlendFactorDestinationAlpha;
		case pipeline::BlendFactor::OneMinusDestinationAlpha: return MTL::BlendFactorOneMinusDestinationAlpha;
		case pipeline::BlendFactor::SourceAlphaSaturated: return MTL::BlendFactorSourceAlphaSaturated;
		case pipeline::BlendFactor::Constant: return MTL::BlendFactorBlendColor;
		case pipeline::BlendFactor::OneMinusConstant: return MTL::BlendFactorOneMinusBlendColor;
		default: return MTL::BlendFactorOne;
		}
	}

	MTL::BlendOperation MapBlendOperation(pipeline::BlendOperation operation) noexcept {
		switch (operation) {
		case pipeline::BlendOperation::Add: return MTL::BlendOperationAdd;
		case pipeline::BlendOperation::Subtract: return MTL::BlendOperationSubtract;
		case pipeline::BlendOperation::ReverseSubtract: return MTL::BlendOperationReverseSubtract;
		case pipeline::BlendOperation::Min: return MTL::BlendOperationMin;
		case pipeline::BlendOperation::Max: return MTL::BlendOperationMax;
		default: return MTL::BlendOperationAdd;
		}
	}

	MTL::ColorWriteMask MapColorWriteMask(ColorWriteMask mask) noexcept {
		MTL::ColorWriteMask result = MTL::ColorWriteMaskNone;
		if ((mask & ColorWriteMask::Red) != ColorWriteMask::None) {
			result |= MTL::ColorWriteMaskRed;
		}
		if ((mask & ColorWriteMask::Green) != ColorWriteMask::None) {
			result |= MTL::ColorWriteMaskGreen;
		}
		if ((mask & ColorWriteMask::Blue) != ColorWriteMask::None) {
			result |= MTL::ColorWriteMaskBlue;
		}
		if ((mask & ColorWriteMask::Alpha) != ColorWriteMask::None) {
			result |= MTL::ColorWriteMaskAlpha;
		}
		return result;
	}

	NS::SharedPtr<MTL::Function> CreateFunction(
		MTL::Device* device,
		shader::SlangCompiledEntryPoint const& entry
	) {
		std::string source_text(
			reinterpret_cast<char const*>(entry.code.data()),
			entry.code.size()
		);
		auto source = NS::String::string(
			source_text.c_str(),
			NS::UTF8StringEncoding
		);
		NS::Error* error = nullptr;
		auto library = NS::TransferPtr(
			device->newLibrary(source, nullptr, &error)
		);
		if (!library) {
			throw std::runtime_error(
				"Failed to create a Metal shader library: " + ErrorMessage(error)
			);
		}
		auto name = NS::String::string(entry.name.c_str(), NS::UTF8StringEncoding);
		auto function = NS::TransferPtr(library->newFunction(name));
		if (!function) {
			throw std::runtime_error(
				"The Metal shader library has no requested entry point"
			);
		}
		return function;
	}

	MTL::Winding MapFrontFace(FrontFace front_face) noexcept {
		if (front_face == FrontFace::CounterClockwise) {
			return MTL::WindingCounterClockwise;
		}
		return MTL::WindingClockwise;
	}

	MTL::CullMode MapCullMode(CullMode cull_mode) noexcept {
		if (cull_mode == CullMode::Front) {
			return MTL::CullModeFront;
		}
		if (cull_mode == CullMode::Back) {
			return MTL::CullModeBack;
		}
		return MTL::CullModeNone;
	}

	std::vector<metal::PipelineBinding> MakeBindings(
		SlangPipelineInterface const& interface
	) {
		std::vector<metal::PipelineBinding> result;
		result.reserve(interface.bindings.size());
		std::ranges::transform(
			interface.bindings,
			std::back_inserter(result),
			[](auto const& binding) {
				return metal::PipelineBinding{
					{
						binding.flags,
						binding.slot,
						binding.space,
						binding.count
					},
					binding.resource_slot,
					binding.sampler_slot,
					binding.visibility
				};
			}
		);
		return result;
	}

} // namespace

namespace fyuu_rhi {

	template <>
	struct CreateBuffer<metal::LogicalDevice> {
		metal::LogicalDevice* logical_device;

		Resource operator()(std::size_t size_in_bytes, ResourceFlags const& flags) const {
			// newBufferWithLength:options: returns a +1 retained MTL::Buffer, so
			// NS::TransferPtr takes ownership without an extra retain.
			auto buffer = NS::TransferPtr(
				logical_device->impl->newBufferWithLength(
					size_in_bytes,
					metal::BufferOptions(flags)
				)
			);
			return MakeResource(
				metal::Resource{ std::move(buffer) },
				size_in_bytes,
				flags
			);
		}
	};

	template <>
	struct CreateTexture<metal::LogicalDevice> {
		metal::LogicalDevice* logical_device;

		Resource operator()(
			std::size_t width,
			std::size_t height,
			std::size_t depth_or_array_layers,
			std::size_t mip_levels,
			ResourceFlags const& flags
		) const {
			auto sample_count = metal::SampleCount(flags);
			if (sample_count > 1u && mip_levels != 1u) {
				throw std::invalid_argument(
					"A Metal multisample texture requires one mip level"
				);
			}
			auto texture_type = metal::TextureType(flags, depth_or_array_layers);
			// alloc()->init() returns a +1 retained descriptor; NS::TransferPtr
			// releases it when the descriptor goes out of scope below.
			auto descriptor = NS::TransferPtr(MTL::TextureDescriptor::alloc()->init());
			descriptor->setTextureType(texture_type);
			descriptor->setPixelFormat(metal::PixelFormat(flags));
			descriptor->setWidth(static_cast<NS::UInteger>(width));
			descriptor->setHeight(static_cast<NS::UInteger>(height));
			descriptor->setDepth(
				texture_type == MTL::TextureType::TextureType3D ?
					static_cast<NS::UInteger>(depth_or_array_layers) :
					1u
			);
			descriptor->setArrayLength(
				texture_type == MTL::TextureType::TextureType3D ?
					1u :
					static_cast<NS::UInteger>(depth_or_array_layers)
			);
			descriptor->setMipmapLevelCount(static_cast<NS::UInteger>(mip_levels));
			descriptor->setSampleCount(static_cast<NS::UInteger>(sample_count));
			descriptor->setUsage(metal::TextureUsage(flags));
			descriptor->setStorageMode(metal::StorageMode(flags));
			auto texture = NS::TransferPtr(
				logical_device->impl->newTexture(descriptor.get())
			);
			return MakeResource(
				metal::Resource{ std::move(texture) },
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
	struct CreateSampler<metal::LogicalDevice> {
		metal::LogicalDevice* logical_device;

		Sampler operator()(SamplerDescriptor const& descriptor) const {
			// alloc()->init() returns a +1 retained descriptor; NS::TransferPtr
			// releases it when the descriptor goes out of scope below.
			auto sampler_descriptor = NS::TransferPtr(MTL::SamplerDescriptor::alloc()->init());
			sampler_descriptor->setSAddressMode(metal::SamplerAddressMode(descriptor.address_mode_u));
			sampler_descriptor->setTAddressMode(metal::SamplerAddressMode(descriptor.address_mode_v));
			sampler_descriptor->setRAddressMode(metal::SamplerAddressMode(descriptor.address_mode_w));
			sampler_descriptor->setMinFilter(metal::Filter(descriptor.min_filter));
			sampler_descriptor->setMagFilter(metal::Filter(descriptor.mag_filter));
			sampler_descriptor->setMipFilter(metal::MipmapFilter(descriptor.mipmap_filter));
			sampler_descriptor->setLodMinClamp(descriptor.min_lod);
			sampler_descriptor->setLodMaxClamp(descriptor.max_lod);
			if (descriptor.max_anisotropy > 1u) {
				sampler_descriptor->setMaxAnisotropy(
					static_cast<NS::UInteger>(descriptor.max_anisotropy)
				);
			}
			sampler_descriptor->setCompareFunction(metal::ComparisonFunction(descriptor.compare_function));
			auto state = NS::TransferPtr(
				logical_device->impl->newSamplerState(sampler_descriptor.get())
			);
			return MakeSampler(metal::Sampler{ std::move(state) });
		}
	};

	template <>
	struct CreateGraphicsPipeline<metal::LogicalDevice> {
		metal::LogicalDevice* logical_device;

		Pipeline operator()(pipeline::GraphicsPipelineDescriptor const& descriptor) const {
			slang::TargetDesc target{ .format = SLANG_METAL };
			shader::SlangProgram program(
				target,
				descriptor.program,
				"metal-msl"
			);
			if (!program.GetInterface().push_constants.empty()) {
				throw std::invalid_argument(
					"Metal pipelines do not support RHI push constants"
				);
			}

			NS::SharedPtr<MTL::Function> vertex_function;
			NS::SharedPtr<MTL::Function> fragment_function;
			std::ranges::for_each(
				program.GetEntryPoints(),
				[&](auto const& entry) {
					if (
						entry.stage != pipeline::Stage::Vertex &&
						entry.stage != pipeline::Stage::Fragment
					) {
						throw std::invalid_argument(
							"Metal graphics pipelines support only vertex and fragment stages"
						);
					}
					auto function = CreateFunction(logical_device->impl.get(), entry);
					if (entry.stage == pipeline::Stage::Vertex) {
						if (vertex_function) {
							throw std::invalid_argument(
								"A Metal graphics pipeline cannot contain duplicate vertex stages"
							);
						}
						vertex_function = std::move(function);
					}
					else {
						if (fragment_function) {
							throw std::invalid_argument(
								"A Metal graphics pipeline cannot contain duplicate fragment stages"
							);
						}
						fragment_function = std::move(function);
					}
				}
			);
			if (!vertex_function) {
				throw std::invalid_argument(
					"A Metal graphics pipeline requires one vertex entry point"
				);
			}

			auto vertex_descriptor = NS::TransferPtr(
				MTL::VertexDescriptor::alloc()->init()
			);
			std::ranges::for_each(
				descriptor.vertex.buffers,
				[&](auto const& buffer) {
					auto layout = vertex_descriptor->layouts()->object(buffer.slot);
					layout->setStride(buffer.stride);
					layout->setStepRate(1u);
					if (buffer.input_rate == pipeline::VertexInputRate::Vertex) {
						layout->setStepFunction(MTL::VertexStepFunctionPerVertex);
					}
					else {
						layout->setStepFunction(MTL::VertexStepFunctionPerInstance);
					}
				}
			);
			std::ranges::for_each(
				descriptor.vertex.attributes,
				[&](auto const& attribute) {
					auto native_attribute = vertex_descriptor->attributes()->object(
						attribute.location
					);
					native_attribute->setFormat(MapVertexFormat(attribute.format));
					native_attribute->setOffset(attribute.offset);
					native_attribute->setBufferIndex(attribute.slot);
				}
			);

			auto pipeline_descriptor = NS::TransferPtr(
				MTL::RenderPipelineDescriptor::alloc()->init()
			);
			pipeline_descriptor->setVertexFunction(vertex_function.get());
			pipeline_descriptor->setFragmentFunction(fragment_function.get());
			pipeline_descriptor->setVertexDescriptor(vertex_descriptor.get());
			pipeline_descriptor->setInputPrimitiveTopology(
				MapTopologyClass(descriptor.primitive.topology)
			);
			pipeline_descriptor->setRasterSampleCount(
				metal::SampleCount(ResourceFlags(descriptor.multisample.sample_count))
			);
			pipeline_descriptor->setAlphaToCoverageEnabled(
				descriptor.multisample.alpha_to_coverage_enabled
			);

			std::ranges::for_each(
				std::views::iota(std::size_t{ 0u }, descriptor.color_targets.size()),
				[&](std::size_t index) {
					auto const& source = descriptor.color_targets[index];
					auto target_attachment = pipeline_descriptor->colorAttachments()->object(
						index
					);
					target_attachment->setPixelFormat(
						metal::PixelFormat(ResourceFlags(source.format))
					);
					target_attachment->setWriteMask(MapColorWriteMask(source.write_mask));
					if (source.blend) {
						target_attachment->setBlendingEnabled(true);
						target_attachment->setRgbBlendOperation(
							MapBlendOperation(source.blend->color.operation)
						);
						target_attachment->setSourceRGBBlendFactor(
							MapBlendFactor(source.blend->color.source_factor)
						);
						target_attachment->setDestinationRGBBlendFactor(
							MapBlendFactor(source.blend->color.destination_factor)
						);
						target_attachment->setAlphaBlendOperation(
							MapBlendOperation(source.blend->alpha.operation)
						);
						target_attachment->setSourceAlphaBlendFactor(
							MapBlendFactor(source.blend->alpha.source_factor)
						);
						target_attachment->setDestinationAlphaBlendFactor(
							MapBlendFactor(source.blend->alpha.destination_factor)
						);
					}
				}
			);

			NS::SharedPtr<MTL::DepthStencilState> depth_stencil;
				if (descriptor.depth_stencil) {
				auto format = metal::PixelFormat(
					ResourceFlags(descriptor.depth_stencil->format)
				);
				using Bits = ResourceFlagBits;
				ResourceFlags format_flags(descriptor.depth_stencil->format);
				if (
					format_flags.Test(Bits::D16Unorm) ||
					format_flags.Test(Bits::D32Float) ||
					format_flags.Test(Bits::D24UnormS8Uint) ||
					format_flags.Test(Bits::D32FloatS8X24Uint)
				) {
					pipeline_descriptor->setDepthAttachmentPixelFormat(format);
				}
				if (
					format_flags.Test(Bits::D24UnormS8Uint) ||
					format_flags.Test(Bits::D32FloatS8X24Uint)
				) {
					pipeline_descriptor->setStencilAttachmentPixelFormat(format);
				}

				auto depth_descriptor = NS::TransferPtr(
					MTL::DepthStencilDescriptor::alloc()->init()
				);
				auto depth_compare = MTL::CompareFunctionAlways;
				if (descriptor.depth_stencil->depth_test_enabled) {
					depth_compare = MapCompareOperation(
						descriptor.depth_stencil->depth_compare
					);
				}
				depth_descriptor->setDepthCompareFunction(
					depth_compare
				);
				depth_descriptor->setDepthWriteEnabled(
					descriptor.depth_stencil->depth_write_enabled
				);
				if (descriptor.depth_stencil->stencil_enabled) {
					auto front = MakeStencilDescriptor(
						descriptor.depth_stencil->stencil_front,
						descriptor.depth_stencil->stencil_read_mask,
						descriptor.depth_stencil->stencil_write_mask
					);
					auto back = MakeStencilDescriptor(
						descriptor.depth_stencil->stencil_back,
						descriptor.depth_stencil->stencil_read_mask,
						descriptor.depth_stencil->stencil_write_mask
					);
					depth_descriptor->setFrontFaceStencil(front.get());
					depth_descriptor->setBackFaceStencil(back.get());
				}
				depth_stencil = NS::TransferPtr(
					logical_device->impl->newDepthStencilState(depth_descriptor.get())
				);
			}

			NS::Error* error = nullptr;
			auto state = NS::TransferPtr(
				logical_device->impl->newRenderPipelineState(
					pipeline_descriptor.get(),
					&error
				)
			);
			if (!state) {
				throw std::runtime_error(
					"Failed to create a Metal render pipeline: " + ErrorMessage(error)
				);
			}
			return MakePipeline(
				metal::Pipeline{
					logical_device->impl,
					std::move(state),
					std::move(depth_stencil),
					MapPrimitiveType(descriptor.primitive.topology),
					MapFrontFace(descriptor.rasterization.front_face),
					MapCullMode(descriptor.rasterization.cull_mode),
					descriptor.rasterization.depth_bias,
					MakeBindings(program.GetInterface())
				}
			);
		}
	};

	template <>
	struct CreateComputePipeline<metal::LogicalDevice> {
		metal::LogicalDevice* logical_device;

		Pipeline operator()(pipeline::ComputePipelineDescriptor const& descriptor) const {
			slang::TargetDesc target{ .format = SLANG_METAL };
			shader::SlangProgram program(
				target,
				descriptor.program,
				"metal-msl"
			);
			if (!program.GetInterface().push_constants.empty()) {
				throw std::invalid_argument(
					"Metal pipelines do not support RHI push constants"
				);
			}
			if (
				program.GetEntryPoints().size() != 1u ||
				program.GetEntryPoints().front().stage != pipeline::Stage::Compute
			) {
				throw std::invalid_argument(
					"A Metal compute pipeline requires exactly one compute entry point"
				);
			}

			auto function = CreateFunction(
				logical_device->impl.get(),
				program.GetEntryPoints().front()
			);
			NS::Error* error = nullptr;
			auto state = NS::TransferPtr(
				logical_device->impl->newComputePipelineState(function.get(), &error)
			);
			if (!state) {
				throw std::runtime_error(
					"Failed to create a Metal compute pipeline: " + ErrorMessage(error)
				);
			}
			return MakePipeline(
				metal::Pipeline{
					logical_device->impl,
					std::move(state),
					{},
					MTL::PrimitiveTypeTriangle,
					MTL::WindingCounterClockwise,
					MTL::CullModeNone,
					{},
					MakeBindings(program.GetInterface())
				}
			);
		}
	};

} // namespace fyuu_rhi
#endif // defined(__APPLE__)
