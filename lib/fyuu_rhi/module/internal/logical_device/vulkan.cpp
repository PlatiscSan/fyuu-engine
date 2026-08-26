module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <vector>

#include <algorithm>
#include <iterator>

#include <cstdint>
#include <array>

#include <optional>
#include <string_view>

#include <ranges>
#include <span>

#include <format>
#endif // !defined(__cpp_lib_modules)
#if !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)
#define FYUU_RHI_USE_VULKAN_HEADER
#include <vulkan/vulkan_shared.hpp>
#endif // !defined(__APPLE__) && defined(__clang__) && defined(_MSVC_STL_VERSION)
#include <slang.h>

module fyuu_rhi:vulkan_logical_device;
#if !defined(__APPLE__)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
#if !defined(FYUU_RHI_USE_VULKAN_HEADER)
import vulkan;
#endif // !defined(FYUU_RHI_USE_VULKAN_HEADER)
import :cache;
import :logical_device_dispatch;
import :pipeline;
import :pipeline_factory;
import :resource_factory;
import :sampler;
import :sampler_factory;
import :slang;
import :vulkan_data;
import :vulkan_utility;

namespace {

	vk::BufferUsageFlags BufferUsage(fyuu_rhi::ResourceFlags const& flags) noexcept {
		using Bits = fyuu_rhi::ResourceFlagBits;
		vk::BufferUsageFlags result;
		if (flags.Test(Bits::CopySRC)) {
			result |= vk::BufferUsageFlagBits::eTransferSrc;
		}
		if (flags.Test(Bits::CopyDST)) {
			result |= vk::BufferUsageFlagBits::eTransferDst;
		}
		if (flags.Test(Bits::UniformTexelBuffer)) {
			result |= vk::BufferUsageFlagBits::eUniformTexelBuffer;
		}
		if (flags.Test(Bits::StorageTexelBuffer)) {
			result |= vk::BufferUsageFlagBits::eStorageTexelBuffer;
		}
		if (flags.Test(Bits::UniformBuffer)) {
			result |= vk::BufferUsageFlagBits::eUniformBuffer;
		}
		if (flags.Test(Bits::StorageBuffer)) {
			result |= vk::BufferUsageFlagBits::eStorageBuffer;
		}
		if (flags.Test(Bits::IndexBuffer)) {
			result |= vk::BufferUsageFlagBits::eIndexBuffer;
		}
		if (flags.Test(Bits::VertexBuffer)) {
			result |= vk::BufferUsageFlagBits::eVertexBuffer;
		}
		if (flags.Test(Bits::IndirectBuffer)) {
			result |= vk::BufferUsageFlagBits::eIndirectBuffer;
		}
		return result;
	}

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::pipeline;

	std::string_view SPIRVProfile(std::uint32_t api_version) noexcept {
		if (api_version >= vk::ApiVersion13) {
			return "spirv_1_6";
		}
		if (api_version >= vk::ApiVersion12) {
			return "spirv_1_5";
		}
		if (api_version >= vk::ApiVersion11) {
			return "spirv_1_3";
		}
		return "spirv_1_0";
	}

	vk::ShaderStageFlagBits ShaderStage(Stage stage) {
		switch (stage) {
		case Stage::Vertex: return vk::ShaderStageFlagBits::eVertex;
		case Stage::Fragment: return vk::ShaderStageFlagBits::eFragment;
		case Stage::TessellationControl: return vk::ShaderStageFlagBits::eTessellationControl;
		case Stage::TessellationEvaluation: return vk::ShaderStageFlagBits::eTessellationEvaluation;
		case Stage::Geometry: return vk::ShaderStageFlagBits::eGeometry;
		case Stage::Compute: return vk::ShaderStageFlagBits::eCompute;
		case Stage::Task: return vk::ShaderStageFlagBits::eTaskEXT;
		case Stage::Mesh: return vk::ShaderStageFlagBits::eMeshEXT;
		default: throw std::invalid_argument("Unsupported Vulkan shader stage");
		}
	}

	vk::ShaderStageFlags ShaderStages(std::uint32_t visibility) noexcept {
		vk::ShaderStageFlags result;
		for (auto stage : {
			Stage::Vertex,
			Stage::Fragment,
			Stage::TessellationControl,
			Stage::TessellationEvaluation,
			Stage::Geometry,
			Stage::Compute,
			Stage::Task,
			Stage::Mesh
		}) {
			if (visibility & (1u << static_cast<std::uint32_t>(stage))) {
				result |= ShaderStage(stage);
			}
		}
		return result;
	}

	vk::DescriptorType DescriptorType(ResourceFlags const& flags) {
		using Bits = ResourceFlagBits;
		if (flags.Test(Bits::SamplerBinding)) {
			return flags.Test(Bits::TextureBinding) ? vk::DescriptorType::eCombinedImageSampler	: vk::DescriptorType::eSampler;
		}
		if (flags.Test(Bits::UniformBuffer)) {
			return vk::DescriptorType::eUniformBuffer;
		}
		if (flags.Test(Bits::StorageBuffer)) {
			return vk::DescriptorType::eStorageBuffer;
		}
		if (flags.Test(Bits::StorageBinding)) {
			return vk::DescriptorType::eStorageImage;
		}
		if (flags.Test(Bits::TextureBinding)) {
			return vk::DescriptorType::eSampledImage;
		}
		throw std::invalid_argument("Unsupported Vulkan pipeline binding flags");
	}

	vk::CompareOp NativeCompareOperation(CompareOperation operation) noexcept {
		return static_cast<vk::CompareOp>(static_cast<std::uint32_t>(operation));
	}

	vk::StencilOp NativeStencilOperation(StencilOperation operation) noexcept {
		constexpr std::array values{
			vk::StencilOp::eKeep,
			vk::StencilOp::eZero,
			vk::StencilOp::eReplace,
			vk::StencilOp::eInvert,
			vk::StencilOp::eIncrementAndClamp,
			vk::StencilOp::eDecrementAndClamp,
			vk::StencilOp::eIncrementAndWrap,
			vk::StencilOp::eDecrementAndWrap
		};
		return values[static_cast<std::size_t>(operation)];
	}

	vk::StencilOpState StencilFace(StencilFaceState const& face) noexcept {
		return {
			NativeStencilOperation(face.fail_operation),
			NativeStencilOperation(face.pass_operation),
			NativeStencilOperation(face.depth_fail_operation),
			NativeCompareOperation(face.compare)
		};
	}

	vk::BlendFactor NativeBlendFactor(BlendFactor factor) noexcept {
		switch (factor) {
		case BlendFactor::Zero: return vk::BlendFactor::eZero;
		case BlendFactor::One: return vk::BlendFactor::eOne;
		case BlendFactor::SourceColor: return vk::BlendFactor::eSrcColor;
		case BlendFactor::OneMinusSourceColor: return vk::BlendFactor::eOneMinusSrcColor;
		case BlendFactor::SourceAlpha: return vk::BlendFactor::eSrcAlpha;
		case BlendFactor::OneMinusSourceAlpha: return vk::BlendFactor::eOneMinusSrcAlpha;
		case BlendFactor::DestinationColor: return vk::BlendFactor::eDstColor;
		case BlendFactor::OneMinusDestinationColor: return vk::BlendFactor::eOneMinusDstColor;
		case BlendFactor::DestinationAlpha: return vk::BlendFactor::eDstAlpha;
		case BlendFactor::OneMinusDestinationAlpha: return vk::BlendFactor::eOneMinusDstAlpha;
		case BlendFactor::SourceAlphaSaturated: return vk::BlendFactor::eSrcAlphaSaturate;
		case BlendFactor::Constant: return vk::BlendFactor::eConstantColor;
		case BlendFactor::OneMinusConstant: return vk::BlendFactor::eOneMinusConstantColor;
		default: return vk::BlendFactor::eOne;
		}
	}

	vk::BlendOp NativeBlendOperation(BlendOperation operation) noexcept {
		switch (operation) {
		case BlendOperation::Add: return vk::BlendOp::eAdd;
		case BlendOperation::Subtract: return vk::BlendOp::eSubtract;
		case BlendOperation::ReverseSubtract: return vk::BlendOp::eReverseSubtract;
		case BlendOperation::Min: return vk::BlendOp::eMin;
		case BlendOperation::Max: return vk::BlendOp::eMax;
		default: return vk::BlendOp::eAdd;
		}
	}

	vk::PrimitiveTopology NativePrimitiveTopology(PrimitiveTopology topology) noexcept {
		switch (topology) {
		case PrimitiveTopology::PointList: return vk::PrimitiveTopology::ePointList;
		case PrimitiveTopology::LineList: return vk::PrimitiveTopology::eLineList;
		case PrimitiveTopology::LineStrip: return vk::PrimitiveTopology::eLineStrip;
		case PrimitiveTopology::TriangleStrip: return vk::PrimitiveTopology::eTriangleStrip;
		default: return vk::PrimitiveTopology::eTriangleList;
		}
	}

	vulkan::Pipeline CreatePipelineInterface(vulkan::LogicalDevice* logical_device, shader::SlangProgram const& program) {
		vulkan::Pipeline result;
		result.dispatcher = logical_device->dispatcher;
		result.bindings = MakePipelineBindingMetadata(program.GetInterface());

		std::uint32_t max_space = 0u;
		for (auto const& binding : program.GetInterface().bindings) {
			max_space = std::max(max_space, binding.space);
		}
		std::vector<std::vector<vk::DescriptorSetLayoutBinding>> set_bindings(
			program.GetInterface().bindings.empty() ? 0u : max_space + 1u
		);
		std::ranges::for_each(
			std::views::iota(std::size_t{ 0u }, set_bindings.size()),
			[&](std::size_t space) {
				auto bindings = program.GetInterface().bindings |
					std::views::filter(
						[space](auto const& binding) {
							return binding.space == space;
						}
					);
				std::ranges::transform(
					bindings,
					std::back_inserter(set_bindings[space]),
					[](auto const& binding) {
						return vk::DescriptorSetLayoutBinding(
							binding.slot,
							DescriptorType(binding.flags),
							binding.count,
							ShaderStages(binding.visibility)
						);
					}
				);
			}
		);

		result.descriptor_set_layouts.reserve(set_bindings.size());
		std::ranges::transform(
			set_bindings,
			std::back_inserter(result.descriptor_set_layouts),
			[logical_device](auto& bindings) {
				std::ranges::sort(
					bindings,
					{},
					&vk::DescriptorSetLayoutBinding::binding
				);
				auto raw_layout = logical_device->impl->createDescriptorSetLayout(
					vk::DescriptorSetLayoutCreateInfo({}, bindings),
					nullptr,
					*logical_device->dispatcher
				);
				return vk::SharedDescriptorSetLayout(
					raw_layout,
					logical_device->impl,
					{ nullptr, *logical_device->dispatcher }
				);
			}
		);
		std::vector<vk::DescriptorSetLayout> raw_layouts;
		raw_layouts.reserve(result.descriptor_set_layouts.size());
		std::ranges::transform(
			result.descriptor_set_layouts,
			std::back_inserter(raw_layouts),
			[](auto const& layout) {
				return *layout;
			}
		);

		std::vector<vk::PushConstantRange> push_constants;
		push_constants.reserve(program.GetInterface().push_constants.size());
		std::ranges::transform(
			program.GetInterface().push_constants,
			std::back_inserter(push_constants),
			[](auto const& range) {
				return vk::PushConstantRange(
					ShaderStages(range.visibility),
					range.offset,
					range.size
				);
			}
		);
		auto raw_pipeline_layout = logical_device->impl->createPipelineLayout(
			vk::PipelineLayoutCreateInfo({}, raw_layouts, push_constants),
			nullptr,
			*logical_device->dispatcher
		);
		result.layout = vk::SharedPipelineLayout(
			raw_pipeline_layout,
			logical_device->impl,
			{ nullptr, *logical_device->dispatcher }
		);
		return result;
	}

	std::vector<vk::SharedShaderModule> CreateShaderModules(
		vulkan::LogicalDevice* logical_device,
		shader::SlangProgram const& program,
		std::vector<vk::PipelineShaderStageCreateInfo>& stages
	) {
		std::vector<vk::SharedShaderModule> modules;
		modules.reserve(program.GetEntryPoints().size());
		stages.reserve(program.GetEntryPoints().size());
		std::ranges::transform(
			program.GetEntryPoints(),
			std::back_inserter(modules),
			[logical_device](auto const& entry) {
				if (entry.code.size() % sizeof(std::uint32_t) != 0u) {
					throw std::runtime_error("Slang returned misaligned SPIR-V bytecode");
				}
				auto raw_module = logical_device->impl->createShaderModule(
					vk::ShaderModuleCreateInfo(
						{},
						entry.code.size(),
						reinterpret_cast<std::uint32_t const*>(entry.code.data())
					),
					nullptr,
					*logical_device->dispatcher
				);
				return vk::SharedShaderModule(
					raw_module,
					logical_device->impl,
					{ nullptr, *logical_device->dispatcher }
				);
			}
		);
		std::ranges::transform(
			std::views::iota(std::size_t{ 0u }, modules.size()),
			std::back_inserter(stages),
			[&](std::size_t index) {
				return vk::PipelineShaderStageCreateInfo(
					vk::PipelineShaderStageCreateFlags{},
					ShaderStage(program.GetEntryPoints()[index].stage),
					*modules[index],
					"main"
				);
			}
		);
		return modules;
	}

} // namespace

namespace fyuu_rhi {

	template <>
	struct CreateBuffer<vulkan::LogicalDevice> {
		vulkan::LogicalDevice* logical_device;

		Resource operator()(std::size_t size_in_bytes, ResourceFlags const& flags) const {
			return MakeResource(
				vulkan::Resource(
					logical_device->dispatcher,
					logical_device->memory_allocator.AllocateBuffer(
						{
							{},
							size_in_bytes,
							BufferUsage(flags),
							vk::SharingMode::eExclusive,
							0u,
							nullptr
						},
						flags
					)
				),
				size_in_bytes,
				flags
			);
		}
	};

	template <>
	struct CreateTexture<vulkan::LogicalDevice> {
		vulkan::LogicalDevice* logical_device;

		Resource operator()(
			std::size_t width,
			std::size_t height,
			std::size_t depth_or_array_layers,
			std::size_t mip_levels,
			ResourceFlags const& flags
		) const {
			auto image_type = vulkan::ImageType(flags);
			auto format = vulkan::ResourceFormat(flags);
			return MakeResource(
				vulkan::Resource(
					logical_device->dispatcher,
					logical_device->memory_allocator.AllocateImage(
						{
							{},
							image_type,
							format,
							{
								static_cast<std::uint32_t>(width),
								static_cast<std::uint32_t>(height),
								image_type == vk::ImageType::e3D ? static_cast<std::uint32_t>(depth_or_array_layers) : 1u
							},
							static_cast<std::uint32_t>(mip_levels),
							image_type == vk::ImageType::e3D ? 1u : static_cast<std::uint32_t>(depth_or_array_layers),
							vulkan::SampleCount(flags),
							vulkan::ImageTiling(flags),
							vulkan::ImageUsage(
								flags,
								format
							),
							vk::SharingMode::eExclusive,
							0u,
							nullptr,
							vk::ImageLayout::eUndefined
						},
						flags
					)
				),
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
	struct CreateSampler<vulkan::LogicalDevice> {
		vulkan::LogicalDevice* logical_device;

		Sampler operator()(SamplerDescriptor const& descriptor) const {
			vk::Sampler raw = logical_device->impl->createSampler(
				{
					{},
					vulkan::Filter(descriptor.mag_filter),
					vulkan::Filter(descriptor.min_filter),
					vulkan::MipmapMode(descriptor.mipmap_filter),
					vulkan::SamplerAddressMode(descriptor.address_mode_u),
					vulkan::SamplerAddressMode(descriptor.address_mode_v),
					vulkan::SamplerAddressMode(descriptor.address_mode_w),
					0.0f,
					descriptor.max_anisotropy > 1u ? vk::True : vk::False,
					static_cast<float>(descriptor.max_anisotropy),
					descriptor.compare_function != CompareFunction::Unknown ? vk::True : vk::False,
					vulkan::ComparisonOperation(descriptor.compare_function),
					descriptor.min_lod,
					descriptor.max_lod,
					vk::BorderColor::eFloatTransparentBlack,
					vk::False
				},
				nullptr,
				*logical_device->dispatcher
			);
			vk::SharedSampler shared(
				raw,
				logical_device->impl,
				{ nullptr, *logical_device->dispatcher }
			);
			return MakeSampler(vulkan::Sampler{ std::move(shared) });
		}
	};

	template <>
	struct CreateGraphicsPipeline<vulkan::LogicalDevice> {
		vulkan::LogicalDevice* logical_device;

		Pipeline operator()(GraphicsPipelineDescriptor const& descriptor) const {
			bool dynamic_rendering_enabled = logical_device->enabled_features.contains(
				vk::StructureType::ePhysicalDeviceDynamicRenderingFeatures
			);

			auto properties = logical_device->physical_device->getProperties(
				*logical_device->dispatcher
			);
			auto spirv_profile = SPIRVProfile(properties.apiVersion);
			slang::TargetDesc target{
				.format = SLANG_SPIRV,
				.profile = shader::SlangGlobalSession()->findProfile(spirv_profile.data())
			};
			auto cache_tag = std::format(
				"vulkan-{:04x}-{:04x}-{:08x}-api-{:08x}-{}",
				properties.vendorID,
				properties.deviceID,
				properties.driverVersion,
				properties.apiVersion,
				spirv_profile
			);
			shader::SlangProgram program(
				target,
				descriptor.program,
				cache_tag
			);
			auto pipeline = CreatePipelineInterface(logical_device, program);

			std::vector<vk::PipelineShaderStageCreateInfo> stages;
			auto modules = CreateShaderModules(logical_device, program, stages);
			if (std::ranges::any_of(
				program.GetEntryPoints(),
				[](auto const& entry) {
					return entry.stage == Stage::Compute;
				}
			)) {
				throw std::invalid_argument("A Vulkan graphics pipeline cannot contain a compute entry point");
			}

			std::vector<vk::VertexInputBindingDescription> vertex_bindings;
			vertex_bindings.reserve(descriptor.vertex.buffers.size());
			std::ranges::transform(
				descriptor.vertex.buffers,
				std::back_inserter(vertex_bindings),
				[](auto const& binding) {
					auto input_rate = vk::VertexInputRate::eInstance;
					if (binding.input_rate == VertexInputRate::Vertex) {
						input_rate = vk::VertexInputRate::eVertex;
					}
					return vk::VertexInputBindingDescription(
						binding.slot,
						binding.stride,
						input_rate
					);
				}
			);
			std::vector<vk::VertexInputAttributeDescription> vertex_attributes;
			vertex_attributes.reserve(descriptor.vertex.attributes.size());
			std::ranges::transform(
				descriptor.vertex.attributes,
				std::back_inserter(vertex_attributes),
				[](auto const& attribute) {
					return vk::VertexInputAttributeDescription(
						attribute.location,
						attribute.slot,
						vulkan::ResourceFormat(ResourceFlags{ attribute.format }),
						attribute.offset
					);
				}
			);
			vk::PipelineVertexInputStateCreateInfo vertex_input(
				{},
				vertex_bindings,
				vertex_attributes
			);
			vk::PipelineInputAssemblyStateCreateInfo input_assembly(
				{},
				NativePrimitiveTopology(descriptor.primitive.topology),
				static_cast<bool>(descriptor.primitive.strip_index_format)
			);
			vk::PipelineViewportStateCreateInfo viewport(
				{},
				1u,
				nullptr,
				1u,
				nullptr
			);
			vk::CullModeFlags cull_mode = vk::CullModeFlagBits::eNone;
			if (descriptor.rasterization.cull_mode == CullMode::Front) {
				cull_mode = vk::CullModeFlagBits::eFront;
			}
			else if (descriptor.rasterization.cull_mode == CullMode::Back) {
				cull_mode = vk::CullModeFlagBits::eBack;
			}
			auto front_face = vk::FrontFace::eClockwise;
			if (descriptor.rasterization.front_face == FrontFace::CounterClockwise) {
				front_face = vk::FrontFace::eCounterClockwise;
			}
			vk::PipelineRasterizationStateCreateInfo rasterization(
				{},
				vk::False,
				vk::False,
				vk::PolygonMode::eFill,
				cull_mode,
				front_face,
				descriptor.rasterization.depth_bias.constant != 0 ||
					descriptor.rasterization.depth_bias.slope_scale != 0.0f,
				static_cast<float>(descriptor.rasterization.depth_bias.constant),
				descriptor.rasterization.depth_bias.clamp,
				descriptor.rasterization.depth_bias.slope_scale,
				1.0f
			);
			auto sample_count = vulkan::SampleCount(
				ResourceFlags{ descriptor.multisample.sample_count }
			);
			vk::PipelineMultisampleStateCreateInfo multisample(
				{},
				sample_count,
				vk::False,
				0.0f,
				&descriptor.multisample.mask,
				descriptor.multisample.alpha_to_coverage_enabled,
				vk::False
			);

			vk::PipelineDepthStencilStateCreateInfo depth_stencil;
			vk::Format depth_format = vk::Format::eUndefined;
			if (descriptor.depth_stencil) {
				depth_format = vulkan::ResourceFormat(
					ResourceFlags{ descriptor.depth_stencil->format }
				);
				auto front = StencilFace(descriptor.depth_stencil->stencil_front);
				auto back = StencilFace(descriptor.depth_stencil->stencil_back);
				front.compareMask = descriptor.depth_stencil->stencil_read_mask;
				front.writeMask = descriptor.depth_stencil->stencil_write_mask;
				back.compareMask = descriptor.depth_stencil->stencil_read_mask;
				back.writeMask = descriptor.depth_stencil->stencil_write_mask;
				depth_stencil = vk::PipelineDepthStencilStateCreateInfo(
					{},
					descriptor.depth_stencil->depth_test_enabled,
					descriptor.depth_stencil->depth_write_enabled,
					NativeCompareOperation(descriptor.depth_stencil->depth_compare),
					vk::False,
					descriptor.depth_stencil->stencil_enabled,
					front,
					back,
					0.0f,
					1.0f
				);
			}

			std::vector<vk::PipelineColorBlendAttachmentState> blend_attachments;
			std::vector<vk::Format> color_formats;
			blend_attachments.reserve(descriptor.color_targets.size());
			color_formats.reserve(descriptor.color_targets.size());
			std::ranges::transform(
				descriptor.color_targets,
				std::back_inserter(color_formats),
				[](auto const& target_state) {
					return vulkan::ResourceFormat(
						ResourceFlags{ target_state.format }
					);
				}
			);
			std::ranges::transform(
				descriptor.color_targets,
				std::back_inserter(blend_attachments),
				[](auto const& target_state) {
					vk::PipelineColorBlendAttachmentState attachment;
					attachment.colorWriteMask = static_cast<vk::ColorComponentFlags>(
						static_cast<std::uint8_t>(target_state.write_mask)
					);
					if (target_state.blend) {
						attachment.blendEnable = vk::True;
						attachment.srcColorBlendFactor = NativeBlendFactor(
							target_state.blend->color.source_factor
						);
						attachment.dstColorBlendFactor = NativeBlendFactor(
							target_state.blend->color.destination_factor
						);
						attachment.colorBlendOp = NativeBlendOperation(
							target_state.blend->color.operation
						);
						attachment.srcAlphaBlendFactor = NativeBlendFactor(
							target_state.blend->alpha.source_factor
						);
						attachment.dstAlphaBlendFactor = NativeBlendFactor(
							target_state.blend->alpha.destination_factor
						);
						attachment.alphaBlendOp = NativeBlendOperation(
							target_state.blend->alpha.operation
						);
					}
					return attachment;
				}
			);
			vk::PipelineColorBlendStateCreateInfo color_blend(
				{},
				vk::False,
				vk::LogicOp::eCopy,
				blend_attachments
			);
			constexpr std::array dynamic_states{
				vk::DynamicState::eViewport,
				vk::DynamicState::eScissor
			};
			vk::PipelineDynamicStateCreateInfo dynamic_state({}, dynamic_states);
			vk::Format stencil_format = vk::Format::eUndefined;
			if (
				depth_format == vk::Format::eD24UnormS8Uint ||
				depth_format == vk::Format::eD32SfloatS8Uint
			) {
				stencil_format = depth_format;
			}
			vk::PipelineRenderingCreateInfo rendering(
				{},
				color_formats,
				depth_format,
				stencil_format
			);
			vk::RenderPass compatible_render_pass;
			if (!dynamic_rendering_enabled) {
				std::vector<vk::AttachmentDescription> attachments;
				std::vector<vk::AttachmentReference> color_references;
				attachments.reserve(
					color_formats.size() +
					(depth_format == vk::Format::eUndefined ? 0u : 1u)
				);
				color_references.reserve(color_formats.size());
				std::ranges::transform(
					color_formats,
					std::back_inserter(attachments),
					[sample_count](auto format) {
						return vk::AttachmentDescription(
							vk::AttachmentDescriptionFlags{},
							format,
							sample_count,
							vk::AttachmentLoadOp::eDontCare,
							vk::AttachmentStoreOp::eStore,
							vk::AttachmentLoadOp::eDontCare,
							vk::AttachmentStoreOp::eDontCare,
							vk::ImageLayout::eUndefined,
							vk::ImageLayout::eColorAttachmentOptimal
						);
					}
				);
				std::ranges::transform(
					std::views::iota(std::size_t{ 0u }, color_formats.size()),
					std::back_inserter(color_references),
					[](std::size_t index) {
						return vk::AttachmentReference(
							static_cast<std::uint32_t>(index),
							vk::ImageLayout::eColorAttachmentOptimal
						);
					}
				);

				std::optional<vk::AttachmentReference> depth_reference;
				if (depth_format != vk::Format::eUndefined) {
					auto index = static_cast<std::uint32_t>(attachments.size());
					attachments.emplace_back(
						vk::AttachmentDescriptionFlags{},
						depth_format,
						sample_count,
						vk::AttachmentLoadOp::eDontCare,
						vk::AttachmentStoreOp::eStore,
						vk::AttachmentLoadOp::eDontCare,
						vk::AttachmentStoreOp::eStore,
						vk::ImageLayout::eUndefined,
						vk::ImageLayout::eDepthStencilAttachmentOptimal
					);
					depth_reference.emplace(
						index,
						vk::ImageLayout::eDepthStencilAttachmentOptimal
					);
				}

				vk::SubpassDescription subpass;
				subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
				subpass.colorAttachmentCount = static_cast<std::uint32_t>(
					color_references.size()
				);
				subpass.pColorAttachments = color_references.data();
				subpass.pDepthStencilAttachment = nullptr;
				if (depth_reference) {
					subpass.pDepthStencilAttachment = &*depth_reference;
				}
				std::array subpasses{ subpass };
				compatible_render_pass = logical_device->impl->createRenderPass(
					vk::RenderPassCreateInfo(
						vk::RenderPassCreateFlags{},
						attachments,
						subpasses
					),
					nullptr,
					*logical_device->dispatcher
				);
				pipeline.compatible_render_pass = vk::SharedRenderPass(
					compatible_render_pass,
					logical_device->impl,
					{ nullptr, *logical_device->dispatcher }
				);
			}
			vk::GraphicsPipelineCreateInfo create_info(
				{},
				stages,
				&vertex_input,
				&input_assembly,
				nullptr,
				&viewport,
				&rasterization,
				&multisample,
				descriptor.depth_stencil ? &depth_stencil : nullptr,
				&color_blend,
				&dynamic_state,
				*pipeline.layout,
				compatible_render_pass
			);
			if (dynamic_rendering_enabled) {
				create_info.pNext = &rendering;
			}

			auto cache_path = cache::GetCacheFilePath(
				std::format(
					"vulkan-pipeline-{:04x}-{:04x}-{:08x}.bin",
					properties.vendorID,
					properties.deviceID,
					properties.driverVersion
				)
			);
			auto cache_data = cache::ReadFile(cache_path);
			auto raw_cache = logical_device->impl->createPipelineCache(
				vk::PipelineCacheCreateInfo(
					{},
					cache_data.size(),
					cache_data.data()
				),
				nullptr,
				*logical_device->dispatcher
			);
			vk::SharedPipelineCache pipeline_cache(
				raw_cache,
				logical_device->impl,
				{ nullptr, *logical_device->dispatcher }
			);
			auto creation = logical_device->impl->createGraphicsPipeline(
				raw_cache,
				create_info,
				nullptr,
				*logical_device->dispatcher
			);
			pipeline.impl = vk::SharedPipeline(
				creation.value,
				logical_device->impl,
				{ nullptr, *logical_device->dispatcher }
			);
			pipeline.bind_point = vk::PipelineBindPoint::eGraphics;
			pipeline.color_formats = color_formats;
			pipeline.depth_stencil_format = depth_format;
			pipeline.samples = sample_count;
			auto updated_cache = logical_device->impl->getPipelineCacheData(
				raw_cache,
				*logical_device->dispatcher
			);
			cache::WriteFileAtomically(
				cache_path,
				std::span<std::byte const>(
					reinterpret_cast<std::byte const*>(updated_cache.data()),
					updated_cache.size()
				)
			);
			return MakePipeline(std::move(pipeline));
		}
	};

	template <>
	struct CreateComputePipeline<vulkan::LogicalDevice> {
		vulkan::LogicalDevice* logical_device;

		Pipeline operator()(ComputePipelineDescriptor const& descriptor) const {
			auto properties = logical_device->physical_device->getProperties(
				*logical_device->dispatcher
			);
			auto spirv_profile = SPIRVProfile(properties.apiVersion);
			slang::TargetDesc target{
				.format = SLANG_SPIRV,
				.profile = shader::SlangGlobalSession()->findProfile(spirv_profile.data())
			};
			auto cache_tag = std::format(
				"vulkan-compute-{:04x}-{:04x}-{:08x}-api-{:08x}-{}",
				properties.vendorID,
				properties.deviceID,
				properties.driverVersion,
				properties.apiVersion,
				spirv_profile
			);
			shader::SlangProgram program(
				target,
				descriptor.program,
				cache_tag
			);
			if (
				program.GetEntryPoints().size() != 1u ||
				program.GetEntryPoints().front().stage != Stage::Compute
			) {
				throw std::invalid_argument(
					"A Vulkan compute pipeline requires exactly one compute entry point"
				);
			}

			auto pipeline = CreatePipelineInterface(logical_device, program);
			std::vector<vk::PipelineShaderStageCreateInfo> stages;
			auto modules = CreateShaderModules(logical_device, program, stages);
			vk::ComputePipelineCreateInfo create_info(
				{},
				stages.front(),
				*pipeline.layout
			);

			auto cache_path = cache::GetCacheFilePath(
				std::format(
					"vulkan-pipeline-{:04x}-{:04x}-{:08x}.bin",
					properties.vendorID,
					properties.deviceID,
					properties.driverVersion
				)
			);
			auto cache_data = cache::ReadFile(cache_path);
			auto raw_cache = logical_device->impl->createPipelineCache(
				vk::PipelineCacheCreateInfo(
					{},
					cache_data.size(),
					cache_data.data()
				),
				nullptr,
				*logical_device->dispatcher
			);
			vk::SharedPipelineCache pipeline_cache(
				raw_cache,
				logical_device->impl,
				{ nullptr, *logical_device->dispatcher }
			);
			auto creation = logical_device->impl->createComputePipeline(
				raw_cache,
				create_info,
				nullptr,
				*logical_device->dispatcher
			);
			pipeline.impl = vk::SharedPipeline(
				creation.value,
				logical_device->impl,
				{ nullptr, *logical_device->dispatcher }
			);
			pipeline.bind_point = vk::PipelineBindPoint::eCompute;
			auto updated_cache = logical_device->impl->getPipelineCacheData(
				raw_cache,
				*logical_device->dispatcher
			);
			cache::WriteFileAtomically(
				cache_path,
				std::span<std::byte const>(
					reinterpret_cast<std::byte const*>(updated_cache.data()),
					updated_cache.size()
				)
			);
			return MakePipeline(std::move(pipeline));
		}
	};

} // namespace fyuu_rhi
#endif // !defined(__APPLE__)
