module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <variant>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#include <webgpu/webgpu_cpp.h>
module fyuu_rhi:webgpu_logical_device;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :webgpu_traits;
import :resource_types;
import :pipeline_types;
import :slang_pipeline_interface;
import :slang;
import :native_pipeline_binding;

namespace {

	using namespace fyuu_rhi;
	using namespace fyuu_rhi::pipeline;

	wgpu::BufferUsage ExtractBufferUsageFlags(ResourceFlags const& flags) {

		wgpu::BufferUsage wgpu_flags{};
	
		if (flags.Test(ResourceFlagBits::CopySRC)) {
			wgpu_flags |= wgpu::BufferUsage::CopySrc;
		}
		if (flags.Test(ResourceFlagBits::CopyDST)) {
			wgpu_flags |= wgpu::BufferUsage::CopyDst;
		}
		if (flags.Test(ResourceFlagBits::UniformTexelBuffer)) {
			wgpu_flags |= wgpu::BufferUsage::TexelBuffer;
		}
		if (flags.Test(ResourceFlagBits::StorageTexelBuffer)) {
			wgpu_flags |= wgpu::BufferUsage::TexelBuffer;
		}
		if (flags.Test(ResourceFlagBits::UniformBuffer)) {
			wgpu_flags |= wgpu::BufferUsage::Uniform;
		}
		if (flags.Test(ResourceFlagBits::StorageBuffer)) {
			wgpu_flags |= wgpu::BufferUsage::Storage;
		}
		if (flags.Test(ResourceFlagBits::IndexBuffer)) {
			wgpu_flags |= wgpu::BufferUsage::Index;
		}
		if (flags.Test(ResourceFlagBits::VertexBuffer)) {
			wgpu_flags |= wgpu::BufferUsage::Vertex;
		}
		if (flags.Test(ResourceFlagBits::IndirectBuffer)) {
			wgpu_flags |= wgpu::BufferUsage::Indirect;
		}

		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::DeviceLocal, ResourceFlagBits::DeviceReadback);
		if (is_conflicting) {
			throw std::invalid_argument("DeviceLocal HostVisible or DeviceReadback are set simultaneously");
		}
	
		if (flags.Test(ResourceFlagBits::HostVisible)) {
			wgpu_flags |= wgpu::BufferUsage::MapWrite;
		}
		else if (flags.Test(ResourceFlagBits::DeviceReadback)) {
			wgpu_flags |= wgpu::BufferUsage::MapRead;
		}
		else {

		}
	
		return wgpu_flags;

	}

	wgpu::TextureUsage ExtractTextureUsage(ResourceFlags const& flags) noexcept {
		
		wgpu::TextureUsage wgpu_flags{};
	
		if (flags.Test(ResourceFlagBits::CopySRC)) {
			wgpu_flags |= wgpu::TextureUsage::CopySrc;
		}
		if (flags.Test(ResourceFlagBits::CopyDST)) {
			wgpu_flags |= wgpu::TextureUsage::CopyDst;
		}
		if (flags.Test(ResourceFlagBits::TextureBinding)) {
			wgpu_flags |= wgpu::TextureUsage::TextureBinding;
		}
		if (flags.Test(ResourceFlagBits::StorageBinding)) {
			wgpu_flags |= wgpu::TextureUsage::StorageBinding;
		}
		if (flags.Test(ResourceFlagBits::RenderAttachment)) {
			wgpu_flags |= wgpu::TextureUsage::RenderAttachment;
		}
		if (flags.Test(ResourceFlagBits::TransientAttachment)) {
			wgpu_flags |= wgpu::TextureUsage::TransientAttachment;
		}
		if (flags.Test(ResourceFlagBits::StorageAttachment)) {
			wgpu_flags |= wgpu::TextureUsage::StorageAttachment;
		}

		return wgpu_flags;

	}

	wgpu::TextureDimension ExtractTextureDimension(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::Texture1D, ResourceFlagBits::Texture3D);
		if (is_conflicting) {
			throw std::invalid_argument("Texture1D Texture2D or Texture3D are set simultaneously");
		}
		if (flags.Test(ResourceFlagBits::Texture1D)) {
			return wgpu::TextureDimension::e1D;
		}
		else if (flags.Test(ResourceFlagBits::Texture2D)) {
			return wgpu::TextureDimension::e2D;
		}
		else if (flags.Test(ResourceFlagBits::Texture3D)) {
			return wgpu::TextureDimension::e3D;
		}
		else {
			return wgpu::TextureDimension::e2D;
		}
	}

	wgpu::TextureFormat ExtractFormat(ResourceFlags const& flags) {

		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::R8Unorm, ResourceFlagBits::Bc7UnormSrgb);
		if (is_conflicting) {
			throw std::invalid_argument("Only one format can be set");
		}
	
		// 8‑bit per component (1‑channel)
		if (flags.Test(ResourceFlagBits::R8Unorm)) return wgpu::TextureFormat::R8Unorm;
		else if (flags.Test(ResourceFlagBits::R8Snorm)) return wgpu::TextureFormat::R8Snorm;
		else if (flags.Test(ResourceFlagBits::R8Uint)) return wgpu::TextureFormat::R8Uint;
		else if (flags.Test(ResourceFlagBits::R8Sint)) return wgpu::TextureFormat::R8Sint;
	
		// 8‑bit per component (2‑channel)
		else if (flags.Test(ResourceFlagBits::R8G8Unorm)) return wgpu::TextureFormat::RG8Unorm;
		else if (flags.Test(ResourceFlagBits::R8G8Snorm)) return wgpu::TextureFormat::RG8Snorm;
		else if (flags.Test(ResourceFlagBits::R8G8Uint)) return wgpu::TextureFormat::RG8Uint;
		else if (flags.Test(ResourceFlagBits::R8G8Sint)) return wgpu::TextureFormat::RG8Sint;
	
		// 8‑bit per component (4‑channel)
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Unorm)) return wgpu::TextureFormat::RGBA8Unorm;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Snorm)) return wgpu::TextureFormat::RGBA8Snorm;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Uint)) return wgpu::TextureFormat::RGBA8Uint;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Sint)) return wgpu::TextureFormat::RGBA8Sint;
		else if (flags.Test(ResourceFlagBits::R8G8B8A8Srgb)) return wgpu::TextureFormat::RGBA8UnormSrgb;
		else if (flags.Test(ResourceFlagBits::B8G8R8A8Srgb)) return wgpu::TextureFormat::BGRA8UnormSrgb;
	
		// 16‑bit per component (1‑channel)
		else if (flags.Test(ResourceFlagBits::R16Unorm)) return wgpu::TextureFormat::R16Unorm;
		else if (flags.Test(ResourceFlagBits::R16Snorm)) return wgpu::TextureFormat::R16Snorm;
		else if (flags.Test(ResourceFlagBits::R16Uint)) return wgpu::TextureFormat::R16Uint;
		else if (flags.Test(ResourceFlagBits::R16Sint)) return wgpu::TextureFormat::R16Sint;
		else if (flags.Test(ResourceFlagBits::R16Float)) return wgpu::TextureFormat::R16Float;
	
		// 16‑bit per component (2‑channel)
		else if (flags.Test(ResourceFlagBits::R16G16Unorm)) return wgpu::TextureFormat::RG16Unorm;
		else if (flags.Test(ResourceFlagBits::R16G16Snorm)) return wgpu::TextureFormat::RG16Snorm;
		else if (flags.Test(ResourceFlagBits::R16G16Uint)) return wgpu::TextureFormat::RG16Uint;
		else if (flags.Test(ResourceFlagBits::R16G16Sint)) return wgpu::TextureFormat::RG16Sint;
		else if (flags.Test(ResourceFlagBits::R16G16Float)) return wgpu::TextureFormat::RG16Float;
	
		// 16‑bit per component (4‑channel)
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Unorm)) return wgpu::TextureFormat::RGBA16Unorm;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Snorm)) return wgpu::TextureFormat::RGBA16Snorm;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Uint)) return wgpu::TextureFormat::RGBA16Uint;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Sint)) return wgpu::TextureFormat::RGBA16Sint;
		else if (flags.Test(ResourceFlagBits::R16G16B16A16Float)) return wgpu::TextureFormat::RGBA16Float;
	
		// 32‑bit per component (1‑channel)
		else if (flags.Test(ResourceFlagBits::R32Uint)) return wgpu::TextureFormat::R32Uint;
		else if (flags.Test(ResourceFlagBits::R32Sint)) return wgpu::TextureFormat::R32Sint;
		else if (flags.Test(ResourceFlagBits::R32Float)) return wgpu::TextureFormat::R32Float;
	
		// 32‑bit per component (2‑channel)
		else if (flags.Test(ResourceFlagBits::R32G32Uint)) return wgpu::TextureFormat::RG32Uint;
		else if (flags.Test(ResourceFlagBits::R32G32Sint)) return wgpu::TextureFormat::RG32Sint;
		else if (flags.Test(ResourceFlagBits::R32G32Float)) return wgpu::TextureFormat::RG32Float;
	
		// 32‑bit per component (4‑channel)
		else if (flags.Test(ResourceFlagBits::R32G32B32A32Uint)) return wgpu::TextureFormat::RGBA32Uint;
		else if (flags.Test(ResourceFlagBits::R32G32B32A32Sint)) return wgpu::TextureFormat::RGBA32Sint;
		else if (flags.Test(ResourceFlagBits::R32G32B32A32Float)) return wgpu::TextureFormat::RGBA32Float;
	
		// Packed formats
		else if (flags.Test(ResourceFlagBits::R10G10B10A2Unorm)) return wgpu::TextureFormat::RGB10A2Unorm;
		else if (flags.Test(ResourceFlagBits::R10G10B10A2Uint)) return wgpu::TextureFormat::RGB10A2Uint;
		else if (flags.Test(ResourceFlagBits::R11G11B10Float)) return wgpu::TextureFormat::RG11B10Ufloat;
		else if (flags.Test(ResourceFlagBits::R9G9B9E5SharedExp)) return wgpu::TextureFormat::RGB9E5Ufloat;
	
		// Depth/stencil
		else if (flags.Test(ResourceFlagBits::D16Unorm)) return wgpu::TextureFormat::Depth16Unorm;
		else if (flags.Test(ResourceFlagBits::D24UnormS8Uint)) return wgpu::TextureFormat::Depth24PlusStencil8;
		else if (flags.Test(ResourceFlagBits::D32Float)) return wgpu::TextureFormat::Depth32Float;
		else if (flags.Test(ResourceFlagBits::D32FloatS8X24Uint)) return wgpu::TextureFormat::Depth32FloatStencil8;
	
		// BC compressed formats
		else if (flags.Test(ResourceFlagBits::Bc1Unorm)) return wgpu::TextureFormat::BC1RGBAUnorm;
		else if (flags.Test(ResourceFlagBits::Bc1UnormSrgb)) return wgpu::TextureFormat::BC1RGBAUnormSrgb;
		else if (flags.Test(ResourceFlagBits::Bc2Unorm)) return wgpu::TextureFormat::BC2RGBAUnorm;
		else if (flags.Test(ResourceFlagBits::Bc2UnormSrgb)) return wgpu::TextureFormat::BC2RGBAUnormSrgb;
		else if (flags.Test(ResourceFlagBits::Bc3Unorm)) return wgpu::TextureFormat::BC3RGBAUnorm;
		else if (flags.Test(ResourceFlagBits::Bc3UnormSrgb)) return wgpu::TextureFormat::BC3RGBAUnormSrgb;
		else if (flags.Test(ResourceFlagBits::Bc4Unorm)) return wgpu::TextureFormat::BC4RUnorm;
		else if (flags.Test(ResourceFlagBits::Bc4Snorm)) return wgpu::TextureFormat::BC4RSnorm;
		else if (flags.Test(ResourceFlagBits::Bc5Unorm)) return wgpu::TextureFormat::BC5RGUnorm;
		else if (flags.Test(ResourceFlagBits::Bc5Snorm)) return wgpu::TextureFormat::BC5RGSnorm;
		else if (flags.Test(ResourceFlagBits::Bc6HUfloat)) return wgpu::TextureFormat::BC6HRGBUfloat;
		else if (flags.Test(ResourceFlagBits::Bc6HSfloat)) return wgpu::TextureFormat::BC6HRGBFloat;
		else if (flags.Test(ResourceFlagBits::Bc7Unorm)) return wgpu::TextureFormat::BC7RGBAUnorm;
		else if (flags.Test(ResourceFlagBits::Bc7UnormSrgb)) return wgpu::TextureFormat::BC7RGBAUnormSrgb;
		
		else return wgpu::TextureFormat::Undefined;

	}

	std::uint32_t ExtractSampleCount(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::Sample1, ResourceFlagBits::Sample64);
		if (is_conflicting) {
			throw std::invalid_argument("Only one sample count can be set");
		}
		if (flags.Test(ResourceFlagBits::Sample1)) return 1u;
		else if (flags.Test(ResourceFlagBits::Sample2)) return 2u;
		else if (flags.Test(ResourceFlagBits::Sample4)) return 4u;
		else if (flags.Test(ResourceFlagBits::Sample8)) return 8u;
		else if (flags.Test(ResourceFlagBits::Sample16)) return 16u;
		else if (flags.Test(ResourceFlagBits::Sample32)) return 32u;
		else if (flags.Test(ResourceFlagBits::Sample64)) return 64u;
		else return 1u;
	}

	wgpu::TextureViewDimension ExtractTextureViewDimension(ResourceFlags const& flags) {
		bool is_conflicting = flags.TestMultipleInRange(ResourceFlagBits::TextureView1D, ResourceFlagBits::TextureView3D);
		if (is_conflicting) {
			throw std::invalid_argument("Only one tex view type can be set");
		}
		if (flags.Test(ResourceFlagBits::TextureView1D))
			return wgpu::TextureViewDimension::e1D;
		else if (flags.Test(ResourceFlagBits::TextureView2D))
			return wgpu::TextureViewDimension::e2D;
		else if (flags.Test(ResourceFlagBits::TextureView2DArray))
			return wgpu::TextureViewDimension::e2DArray;
		else if (flags.Test(ResourceFlagBits::TextureViewCube))
			return wgpu::TextureViewDimension::Cube;
		else if (flags.Test(ResourceFlagBits::TextureViewCubeArray))
			return wgpu::TextureViewDimension::CubeArray;
		else if (flags.Test(ResourceFlagBits::TextureView3D))
			return wgpu::TextureViewDimension::e3D;
		else
			return wgpu::TextureViewDimension::e2D;
	}

	wgpu::TextureAspect ExtractTextureViewAspect(ResourceFlags const& flags) {
		if (flags.Test(ResourceFlagBits::TextureViewAspectDepthOnly))
			return wgpu::TextureAspect::DepthOnly;
		if (flags.Test(ResourceFlagBits::TextureViewAspectStencilOnly))
			return wgpu::TextureAspect::StencilOnly;
		if (flags.Test(ResourceFlagBits::TextureViewAspectPlane0Only))
			return wgpu::TextureAspect::Plane0Only;
		if (flags.Test(ResourceFlagBits::TextureViewAspectPlane1Only))
			return wgpu::TextureAspect::Plane1Only;
		if (flags.Test(ResourceFlagBits::TextureViewAspectPlane2Only))
			return wgpu::TextureAspect::Plane2Only;
		return wgpu::TextureAspect::All;
	}

	wgpu::AddressMode MapAddressMode(AddressMode mode) noexcept {
		switch (mode) {
		case AddressMode::ClampToEdge:		return wgpu::AddressMode::ClampToEdge;
		case AddressMode::Repeat:			return wgpu::AddressMode::Repeat;
		case AddressMode::MirroredRepeat:	return wgpu::AddressMode::MirrorRepeat;
		default:							return wgpu::AddressMode::ClampToEdge;
		}
	}

	wgpu::FilterMode MapFilterMode(FilterMode mode) noexcept {
		switch (mode) {
		case FilterMode::Nearest:	return wgpu::FilterMode::Nearest;
		case FilterMode::Linear:	return wgpu::FilterMode::Linear;
		default:					return wgpu::FilterMode::Linear;
		}
	}

	wgpu::MipmapFilterMode MapMipmapMode(MipmapFilterMode mode) noexcept {
		switch (mode) {
		case MipmapFilterMode::Nearest: return wgpu::MipmapFilterMode::Nearest;
		case MipmapFilterMode::Linear:	return wgpu::MipmapFilterMode::Linear;
		default:						return wgpu::MipmapFilterMode::Linear;
		}
	}

	wgpu::CompareFunction MapCompare(CompareFunction func) noexcept {
		switch (func) {
		case CompareFunction::Never:		return wgpu::CompareFunction::Never;
		case CompareFunction::Less:			return wgpu::CompareFunction::Less;
		case CompareFunction::Equal:		return wgpu::CompareFunction::Equal;
		case CompareFunction::LessEqual:	return wgpu::CompareFunction::LessEqual;
		case CompareFunction::Greater:		return wgpu::CompareFunction::Greater;
		case CompareFunction::NotEqual:		return wgpu::CompareFunction::NotEqual;
		case CompareFunction::GreaterEqual: return wgpu::CompareFunction::GreaterEqual;
		case CompareFunction::Always:		return wgpu::CompareFunction::Always;
		default:							return wgpu::CompareFunction::Undefined;
		}
	}

	wgpu::CompareFunction MapPipelineCompare(CompareOperation operation) noexcept {
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

	wgpu::StencilOperation MapStencilOperation(StencilOperation operation) noexcept {
		switch (operation) {
		case StencilOperation::Keep: return wgpu::StencilOperation::Keep;
		case StencilOperation::Zero: return wgpu::StencilOperation::Zero;
		case StencilOperation::Replace: return wgpu::StencilOperation::Replace;
		case StencilOperation::Invert: return wgpu::StencilOperation::Invert;
		case StencilOperation::IncrementClamp: return wgpu::StencilOperation::IncrementClamp;
		case StencilOperation::DecrementClamp: return wgpu::StencilOperation::DecrementClamp;
		case StencilOperation::IncrementWrap: return wgpu::StencilOperation::IncrementWrap;
		case StencilOperation::DecrementWrap: return wgpu::StencilOperation::DecrementWrap;
		default: return wgpu::StencilOperation::Keep;
		}
	}

	wgpu::StencilFaceState MapStencilFace(StencilFaceState const& state) noexcept {
		return {
			.compare = MapPipelineCompare(state.compare),
			.failOp = MapStencilOperation(state.fail_operation),
			.depthFailOp = MapStencilOperation(state.depth_fail_operation),
			.passOp = MapStencilOperation(state.pass_operation)
		};
	}

	wgpu::BlendFactor MapBlendFactor(BlendFactor factor) noexcept {
		switch (factor) {
		case BlendFactor::Zero: return wgpu::BlendFactor::Zero;
		case BlendFactor::One: return wgpu::BlendFactor::One;
		case BlendFactor::SourceColor: return wgpu::BlendFactor::Src;
		case BlendFactor::OneMinusSourceColor: return wgpu::BlendFactor::OneMinusSrc;
		case BlendFactor::SourceAlpha: return wgpu::BlendFactor::SrcAlpha;
		case BlendFactor::OneMinusSourceAlpha: return wgpu::BlendFactor::OneMinusSrcAlpha;
		case BlendFactor::DestinationColor: return wgpu::BlendFactor::Dst;
		case BlendFactor::OneMinusDestinationColor: return wgpu::BlendFactor::OneMinusDst;
		case BlendFactor::DestinationAlpha: return wgpu::BlendFactor::DstAlpha;
		case BlendFactor::OneMinusDestinationAlpha: return wgpu::BlendFactor::OneMinusDstAlpha;
		case BlendFactor::SourceAlphaSaturated: return wgpu::BlendFactor::SrcAlphaSaturated;
		case BlendFactor::Constant: return wgpu::BlendFactor::Constant;
		case BlendFactor::OneMinusConstant: return wgpu::BlendFactor::OneMinusConstant;
		default: return wgpu::BlendFactor::One;
		}
	}

	wgpu::BlendOperation MapBlendOperation(BlendOperation operation) noexcept {
		switch (operation) {
		case BlendOperation::Add: return wgpu::BlendOperation::Add;
		case BlendOperation::Subtract: return wgpu::BlendOperation::Subtract;
		case BlendOperation::ReverseSubtract: return wgpu::BlendOperation::ReverseSubtract;
		case BlendOperation::Min: return wgpu::BlendOperation::Min;
		case BlendOperation::Max: return wgpu::BlendOperation::Max;
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
}

namespace fyuu_rhi::webgpu {
	using namespace fyuu_rhi::pipeline;

	Backend::Resource Backend::CreateBuffer(LogicalDevice const& ld, std::size_t size_in_bytes, ResourceFlags const& flags) {
		wgpu::BufferDescriptor desc{
			nullptr,
			{},
			ExtractBufferUsageFlags(flags),
			size_in_bytes,
			false
		};
		return Backend::Resource(ld.impl.CreateBuffer(&desc));
	}

	Backend::Resource Backend::CreateTexture(LogicalDevice const& ld, std::size_t width, std::size_t height, std::size_t depth_arr_layers, std::size_t mip_lvl_cnt, ResourceFlags const& flags) {
		wgpu::TextureDescriptor desc{
			nullptr,
			{},
			ExtractTextureUsage(flags),
			ExtractTextureDimension(flags),
			{ static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), static_cast<std::uint32_t>(depth_arr_layers) },
			ExtractFormat(flags),
			static_cast<std::uint32_t>(mip_lvl_cnt),
			ExtractSampleCount(flags),
			0,
			nullptr
		};
		return Backend::Resource(ld.impl.CreateTexture(&desc));
	}

	Backend::View Backend::CreateTextureView(LogicalDevice const& ld, Backend::Resource const& res, std::size_t base_mip_lvl, std::size_t mip_lvl_cnt, std::size_t base_arr_layer, std::size_t arr_layer_cnt, ResourceFlags const& flags) {
		if (!ld.impl) throw std::invalid_argument("WebGPU logical device must not be empty");
		wgpu::Texture const& tex = std::get<wgpu::Texture>(res);

		wgpu::TextureViewDescriptor view_desc = {
			.format = ExtractFormat(flags),
			.dimension = ExtractTextureViewDimension(flags),
			.baseMipLevel = static_cast<std::uint32_t>(base_mip_lvl),
			.mipLevelCount = static_cast<std::uint32_t>(mip_lvl_cnt),
			.baseArrayLayer = static_cast<std::uint32_t>(base_arr_layer),
			.arrayLayerCount = static_cast<std::uint32_t>(arr_layer_cnt),
			.aspect = ExtractTextureViewAspect(flags)
		};
	
		return Backend::View(tex.CreateView(&view_desc));

	}

	Backend::View Backend::CreateBufferView(LogicalDevice const& ld, Backend::Resource const& buf, std::size_t offset, std::size_t range, ResourceFlags const& flags) {
		if (!ld.impl) throw std::invalid_argument("WebGPU logical device must not be empty");
		auto const& buffer = std::get<wgpu::Buffer>(buf);
		if (offset + range > buffer.GetSize()) {
			throw std::out_of_range("WebGPU buffer view exceeds the source buffer");
		}
		return Backend::View(Backend::BufferView{ buffer, offset, range });
	}

	wgpu::Sampler Backend::CreateSampler(LogicalDevice const& ld, SamplerDescriptor const& desc) {
		wgpu::SamplerDescriptor wgpu_desc = {
			.addressModeU = MapAddressMode(desc.address_mode_u),
			.addressModeV = MapAddressMode(desc.address_mode_v),
			.addressModeW = MapAddressMode(desc.address_mode_w),
			.magFilter = MapFilterMode(desc.mag_filter),
			.minFilter = MapFilterMode(desc.min_filter),
			.mipmapFilter = MapMipmapMode(desc.mipmap_filter),
			.lodMinClamp = desc.min_lod,
			.lodMaxClamp = desc.max_lod,
			.compare = MapCompare(desc.compare_function),
			.maxAnisotropy = desc.max_anisotropy,
		};
		return ld.impl.CreateSampler(&wgpu_desc);
	}

	Backend::Pipeline Backend::CreateGraphicsPipeline(
		LogicalDevice const& ld,
		GraphicsPipelineDescriptor const& descriptor
	) {
		if (!descriptor.program.entry_points.empty()) {
			for (auto const& entry : descriptor.program.entry_points) {
				if (entry.stage != PipelineStage::Vertex && entry.stage != PipelineStage::Fragment) {
					throw std::invalid_argument("WebGPU graphics pipelines support only vertex and fragment stages");
				}
			}
		}
		slang::TargetDesc target{ .format = SLANG_WGSL };
		shader::SlangProgram program(target, descriptor.program, "webgpu-wgsl");
		if (!program.Interface().push_constants.empty()) {
			throw std::invalid_argument("WebGPU does not support push constants");
		}

		std::vector<wgpu::ShaderModule> modules;
		modules.reserve(program.EntryPoints().size());
		wgpu::ShaderModule vertex_module;
		wgpu::ShaderModule fragment_module;
		wgpu::StringView vertex_entry;
		wgpu::StringView fragment_entry;
		for (auto const& entry : program.EntryPoints()) {
			wgpu::ShaderSourceWGSL source;
			source.code = {
				reinterpret_cast<char const*>(entry.code.data()),
				entry.code.size()
			};
			wgpu::ShaderModuleDescriptor module_descriptor{
				.nextInChain = &source
			};
			auto module = ld.impl.CreateShaderModule(&module_descriptor);
			modules.push_back(module);
			if (entry.stage == PipelineStage::Vertex) {
				vertex_module = module;
				vertex_entry = { entry.name.data(), entry.name.size() };
			}
			else {
				fragment_module = module;
				fragment_entry = { entry.name.data(), entry.name.size() };
			}
		}
		if (!vertex_module) {
			throw std::invalid_argument("WebGPU graphics pipeline has no vertex entry point");
		}

		std::vector<std::vector<wgpu::VertexAttribute>> attributes(descriptor.vertex.buffers.size());
		std::vector<wgpu::VertexBufferLayout> buffers;
		buffers.reserve(descriptor.vertex.buffers.size());
		for (std::size_t index = 0; index < descriptor.vertex.buffers.size(); ++index) {
			auto const& buffer = descriptor.vertex.buffers[index];
			for (auto const& attribute : descriptor.vertex.attributes) {
				if (attribute.slot == buffer.slot) {
					attributes[index].push_back(
						{
							.format = MapVertexFormat(attribute.format),
							.offset = attribute.offset,
							.shaderLocation = attribute.location
						}
					);
				}
			}
			buffers.push_back(
				{
					.stepMode = buffer.input_rate == VertexInputRate::Vertex
						? wgpu::VertexStepMode::Vertex
						: wgpu::VertexStepMode::Instance,
					.arrayStride = buffer.stride,
					.attributeCount = attributes[index].size(),
					.attributes = attributes[index].data()
				}
			);
		}

		wgpu::VertexState vertex{
			.module = vertex_module,
			.entryPoint = vertex_entry,
			.bufferCount = buffers.size(),
			.buffers = buffers.data()
		};
		wgpu::PrimitiveState primitive{
			.topology =
				descriptor.primitive.topology == PrimitiveTopology::PointList ? wgpu::PrimitiveTopology::PointList :
				descriptor.primitive.topology == PrimitiveTopology::LineList ? wgpu::PrimitiveTopology::LineList :
				descriptor.primitive.topology == PrimitiveTopology::LineStrip ? wgpu::PrimitiveTopology::LineStrip :
				descriptor.primitive.topology == PrimitiveTopology::TriangleStrip ? wgpu::PrimitiveTopology::TriangleStrip :
				wgpu::PrimitiveTopology::TriangleList,
			.stripIndexFormat = !descriptor.primitive.strip_index_format
				? wgpu::IndexFormat::Undefined
				: *descriptor.primitive.strip_index_format == IndexFormat::Uint16
					? wgpu::IndexFormat::Uint16
					: wgpu::IndexFormat::Uint32,
			.frontFace = descriptor.rasterization.front_face == FrontFace::CounterClockwise
				? wgpu::FrontFace::CCW
				: wgpu::FrontFace::CW,
			.cullMode =
				descriptor.rasterization.cull_mode == CullMode::Front ? wgpu::CullMode::Front :
				descriptor.rasterization.cull_mode == CullMode::Back ? wgpu::CullMode::Back :
				wgpu::CullMode::None
		};

		std::optional<wgpu::DepthStencilState> depth_stencil;
		if (descriptor.depth_stencil) {
			auto const& state = *descriptor.depth_stencil;
			depth_stencil = {
				.format = ExtractFormat(ResourceFlags(state.format)),
				.depthWriteEnabled = state.depth_write_enabled
					? wgpu::OptionalBool::True
					: wgpu::OptionalBool::False,
				.depthCompare = state.depth_test_enabled
					? MapPipelineCompare(state.depth_compare)
					: wgpu::CompareFunction::Always,
				.stencilFront = MapStencilFace(state.stencil_front),
				.stencilBack = MapStencilFace(state.stencil_back),
				.stencilReadMask = state.stencil_read_mask,
				.stencilWriteMask = state.stencil_write_mask,
				.depthBias = descriptor.rasterization.depth_bias.constant,
				.depthBiasSlopeScale = descriptor.rasterization.depth_bias.slope_scale,
				.depthBiasClamp = descriptor.rasterization.depth_bias.clamp
			};
		}

		std::vector<wgpu::BlendState> blends;
		std::vector<wgpu::ColorTargetState> color_targets;
		blends.reserve(descriptor.color_targets.size());
		color_targets.reserve(descriptor.color_targets.size());
		for (auto const& target_state : descriptor.color_targets) {
			wgpu::BlendState* blend = nullptr;
			if (target_state.blend) {
				auto const& state = *target_state.blend;
				blends.push_back(
					{
						.color = {
							.operation = MapBlendOperation(state.color.operation),
							.srcFactor = MapBlendFactor(state.color.source_factor),
							.dstFactor = MapBlendFactor(state.color.destination_factor)
						},
						.alpha = {
							.operation = MapBlendOperation(state.alpha.operation),
							.srcFactor = MapBlendFactor(state.alpha.source_factor),
							.dstFactor = MapBlendFactor(state.alpha.destination_factor)
						}
					}
				);
				blend = &blends.back();
			}
			color_targets.push_back(
				{
					.format = ExtractFormat(ResourceFlags(target_state.format)),
					.blend = blend,
					.writeMask = static_cast<wgpu::ColorWriteMask>(static_cast<std::uint8_t>(target_state.write_mask))
				}
			);
		}
		std::optional<wgpu::FragmentState> fragment;
		if (fragment_module) {
			fragment = {
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
				.count = ExtractSampleCount(ResourceFlags(descriptor.multisample.sample_count)),
				.mask = descriptor.multisample.mask,
				.alphaToCoverageEnabled = descriptor.multisample.alpha_to_coverage_enabled
			},
			.fragment = fragment ? &*fragment : nullptr
		};

		Backend::Pipeline pipeline;
		pipeline.bindings = MakePipelineBindingMetadata(program.Interface());
		pipeline.impl = ld.impl.CreateRenderPipeline(&pipeline_descriptor);
		std::uint32_t bind_group_count = 0;
		for (auto const& binding : program.Interface().bindings) {
			bind_group_count = std::max(bind_group_count, binding.space + 1);
		}
		pipeline.bind_group_layouts.reserve(bind_group_count);
		for (std::uint32_t index = 0; index < bind_group_count; ++index) {
			pipeline.bind_group_layouts.push_back(
				std::get<wgpu::RenderPipeline>(pipeline.impl).GetBindGroupLayout(index)
			);
		}
		return pipeline;
	}

	Backend::Pipeline Backend::CreateComputePipeline(
		LogicalDevice const& ld,
		ComputePipelineDescriptor const& descriptor
	) {
		slang::TargetDesc target{ .format = SLANG_WGSL };
		shader::SlangProgram program(target, descriptor.program, "webgpu-wgsl");
		if (!program.Interface().push_constants.empty()) {
			throw std::invalid_argument("WebGPU does not support push constants");
		}
		if (program.EntryPoints().size() != 1u ||
			program.EntryPoints().front().stage != PipelineStage::Compute) {
			throw std::invalid_argument("WebGPU compute pipelines require one compute entry point");
		}
		auto const& entry = program.EntryPoints().front();
		wgpu::ShaderSourceWGSL source;
		source.code = {
			reinterpret_cast<char const*>(entry.code.data()),
			entry.code.size()
		};
		wgpu::ShaderModuleDescriptor module_descriptor{ .nextInChain = &source };
		auto module = ld.impl.CreateShaderModule(&module_descriptor);
		wgpu::ComputePipelineDescriptor pipeline_descriptor{
			.compute = {
				.module = module,
				.entryPoint = { entry.name.data(), entry.name.size() }
			}
		};
		Backend::Pipeline pipeline;
		pipeline.compute = true;
		pipeline.bindings = MakePipelineBindingMetadata(program.Interface());
		pipeline.impl = ld.impl.CreateComputePipeline(&pipeline_descriptor);
		std::uint32_t bind_group_count = 0u;
		for (auto const& binding : program.Interface().bindings) {
			bind_group_count = std::max(bind_group_count, binding.space + 1u);
		}
		pipeline.bind_group_layouts.reserve(bind_group_count);
		for (std::uint32_t index = 0u; index < bind_group_count; ++index) {
			pipeline.bind_group_layouts.emplace_back(
				std::get<wgpu::ComputePipeline>(pipeline.impl).GetBindGroupLayout(index)
			);
		}
		return pipeline;
	}

	Backend::PipelineResourceGroup Backend::CreatePipelineResourceGroup(
		LogicalDevice const& ld,
		Backend::Pipeline const& pipeline,
		std::uint32_t space,
		std::span<NativePipelineResourceBinding<Backend> const> bindings
	) {
		if (!ld.impl) {
			throw std::invalid_argument("WebGPU logical device must not be empty");
		}
		if (space >= pipeline.bind_group_layouts.size()) {
			throw std::out_of_range("WebGPU pipeline resource group space is unavailable");
		}
		Backend::PipelineResourceGroup result;
		result.native = MakePipelineResourceGroup<Backend>(pipeline.bindings, space, bindings);
		result.space = space;
		std::vector<wgpu::BindGroupEntry> entries;
		entries.reserve(result.native.bindings.size());
		for (auto const& resource_binding : result.native.bindings) {
			if (resource_binding.array_element != 0u) {
				throw std::invalid_argument("WebGPU binding arrays are not implemented");
			}
			wgpu::BindGroupEntry entry{ .binding = resource_binding.slot };
			if (auto buffer = std::get_if<NativePipelineBufferBinding<Backend>>(&resource_binding.value)) {
				entry.buffer = std::get<wgpu::Buffer>(buffer->impl.get());
				entry.offset = buffer->offset;
				entry.size = buffer->size == PipelineWholeBuffer ? wgpu::kWholeSize : buffer->size;
			}
			else if (auto view = std::get_if<NativePipelineViewBinding<Backend>>(&resource_binding.value)) {
				if (auto texture = std::get_if<wgpu::TextureView>(&view->get())) {
					entry.textureView = *texture;
				}
				else if (auto buffer_view = std::get_if<Backend::BufferView>(&view->get())) {
					entry.buffer = buffer_view->buf;
					entry.offset = buffer_view->offset;
					entry.size = buffer_view->range;
				}
				else throw std::invalid_argument("WebGPU resource binding has an empty view");
			}
			else if (auto sampler = std::get_if<NativePipelineSamplerBinding<Backend>>(&resource_binding.value)) {
				entry.sampler = sampler->get();
			}
			else if (std::holds_alternative<NativePipelineCombinedBinding<Backend>>(resource_binding.value)) {
				throw std::invalid_argument("WebGPU requires separate texture and sampler bindings");
			}
			entries.emplace_back(entry);
		}
		wgpu::BindGroupDescriptor descriptor{
			.layout = pipeline.bind_group_layouts[space],
			.entryCount = entries.size(),
			.entries = entries.data()
		};
		result.impl = ld.impl.CreateBindGroup(&descriptor);
		return result;
	}

	Backend::Scheduler Backend::CreateScheduler(
		LogicalDevice const& ld,
		SchedulerDescriptor const& descriptor
	) {
		using Flag = SchedulerFlagBits;
		if (!descriptor.flags.Test(Flag::Graphics) &&
			!descriptor.flags.Test(Flag::Compute) &&
			!descriptor.flags.Test(Flag::Copy)) {
			throw std::invalid_argument("CreateScheduler(): no execution capability was requested");
		}
		auto queue = std::make_shared<WebGPUScheduler::QueueState>(ld.impl, ld.GetQueue());
		WebGPUScheduler::QueueCollection queues;
		if (descriptor.flags.Test(Flag::Graphics)) {
			queues.graphics = queue;
		}
		if (descriptor.flags.Test(Flag::Compute)) {
			queues.compute = queue;
		}
		if (descriptor.flags.Test(Flag::Copy)) {
			queues.copy = queue;
		}
		return std::make_shared<WebGPUScheduler>(
			queues,
			ld.adapter,
			ld.presentation_cache,
			ld.completion_service
		);
	}

	Backend::CommandGraph Backend::CreateCommandGraph(
		execution::CommandGraphDescriptor const& descriptor
	) {
		return execution::MakeCommandGraph<Backend>(descriptor);
	}

}
