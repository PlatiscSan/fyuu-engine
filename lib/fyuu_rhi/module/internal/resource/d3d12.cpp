module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <utility>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#endif // defined(_WIN32)

module fyuu_rhi:d3d12_resource;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :d3d12_data;
import :d3d12_descriptor_allocator;
import :d3d12_utility;
import :resource_dispatch;
import :view_factory;

namespace {

	Microsoft::WRL::ComPtr<ID3D12Device> GetDevice(ID3D12Resource* resource) {
		Microsoft::WRL::ComPtr<ID3D12Device> device;
		fyuu_rhi::d3d12::ThrowIfFailed(resource->GetDevice(IID_PPV_ARGS(&device)));
		return device;
	}

} // namespace

namespace fyuu_rhi {

	template <>
	struct CreateBufferView<d3d12::Resource> {
		d3d12::Resource* resource;

		View operator()(std::size_t offset, std::size_t range, ResourceFlags const& flags) const {
			using Type = d3d12::View::Type;
			using Bits = ResourceFlagBits;
			ID3D12Resource* buf = resource->allocation->GetResource();
			auto device = GetDevice(buf);
			DXGI_FORMAT format = d3d12::ResourceFormat(flags, false);

			bool is_uav =
				flags.Test(Bits::StorageBuffer) ||
				flags.Test(Bits::StorageTexelBuffer) ||
				flags.Test(Bits::StorageBinding);
			bool is_srv = flags.Test(Bits::TextureBinding);
			if (!is_uav && !is_srv) {
				// Buffer SRV uses the same flag as a texture; default to SRV when no
				// explicit view type is given.
				is_srv = true;
			}

			d3d12::View result;
			if (is_srv) {
				auto handle = resource->resource_descriptors.Allocate();
				D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {
					.ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
					.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
					.Buffer = { 0u, 0u, 0u, D3D12_BUFFER_SRV_FLAG_NONE }
				};
				if (format == DXGI_FORMAT_UNKNOWN) {
					// Raw buffer view (byte addressable, 32-bit elements).
					if ((offset % 4u) != 0u || (range % 4u) != 0u) {
						throw std::invalid_argument(
							"A D3D12 raw buffer SRV requires 4-byte aligned offset and range"
						);
					}
					srv_desc.Format = DXGI_FORMAT_R32_TYPELESS;
					srv_desc.Buffer.FirstElement = static_cast<UINT>(offset / 4u);
					srv_desc.Buffer.NumElements = static_cast<UINT>(range / 4u);
					srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
				}
				else {
					auto element_size = d3d12::FormatSize(format);
					if (element_size == 0u) {
						throw std::invalid_argument(
							"A D3D12 typed buffer SRV requires a known element size"
						);
					}
					if ((offset % element_size) != 0u || (range % element_size) != 0u) {
						throw std::invalid_argument(
							"A D3D12 typed buffer SRV offset and range must align to the element size"
						);
					}
					srv_desc.Format = format;
					srv_desc.Buffer.FirstElement = static_cast<UINT>(offset / element_size);
					srv_desc.Buffer.NumElements = static_cast<UINT>(range / element_size);
				}
				device->CreateShaderResourceView(buf, &srv_desc, handle.CPU());
				result.descriptors[static_cast<std::size_t>(Type::ShaderResource)] = std::move(handle);
			}
			if (is_uav) {
				auto handle = resource->resource_descriptors.Allocate();
				D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
					.ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
					.Buffer = { 0u, 0u, 0u, D3D12_BUFFER_UAV_FLAG_NONE }
				};
				if (format == DXGI_FORMAT_UNKNOWN) {
					// Raw buffer UAV.
					if ((offset % 4u) != 0u || (range % 4u) != 0u) {
						throw std::invalid_argument(
							"A D3D12 raw buffer UAV requires 4-byte aligned offset and range"
						);
					}
					uav_desc.Format = DXGI_FORMAT_R32_TYPELESS;
					uav_desc.Buffer.FirstElement = static_cast<UINT>(offset / 4u);
					uav_desc.Buffer.NumElements = static_cast<UINT>(range / 4u);
					uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
				}
				else {
					auto element_size = d3d12::FormatSize(format);
					if (element_size == 0u) {
						throw std::invalid_argument(
							"A D3D12 typed buffer UAV requires a known element size"
						);
					}
					if ((offset % element_size) != 0u || (range % element_size) != 0u) {
						throw std::invalid_argument(
							"A D3D12 typed buffer UAV offset and range must align to the element size"
						);
					}
					uav_desc.Format = format;
					uav_desc.Buffer.FirstElement = static_cast<UINT>(offset / element_size);
					uav_desc.Buffer.NumElements = static_cast<UINT>(range / element_size);
				}
				device->CreateUnorderedAccessView(buf, nullptr, &uav_desc, handle.CPU());
				result.descriptors[static_cast<std::size_t>(Type::UnorderedAccess)] = std::move(handle);
			}
			return MakeView(std::move(result));
		}
	};

	template <>
	struct CreateTextureView<d3d12::Resource> {
		d3d12::Resource* resource;

		View operator()(
			std::size_t base_mip_lvl,
			std::size_t mip_lvl_cnt,
			std::size_t base_arr_layer,
			std::size_t arr_layer_cnt,
			ResourceFlags const& flags
		) const {
			using Type = d3d12::View::Type;
			using Bits = ResourceFlagBits;
			ID3D12Resource* tex = resource->allocation->GetResource();
			auto device = GetDevice(tex);
			DXGI_FORMAT format = d3d12::ResourceFormat(flags, true);

			bool is_depth_stencil = d3d12::IsDepthStencil(format);
			bool is_srv = flags.Test(Bits::TextureBinding);
			bool is_uav = flags.Test(Bits::StorageBinding);
			bool is_rtv = flags.Test(Bits::RenderAttachment) && !is_depth_stencil;
			bool is_dsv = flags.Test(Bits::RenderAttachment) && is_depth_stencil;
			if (!is_srv && !is_uav && !is_rtv && !is_dsv) {
				throw std::invalid_argument(
					"A D3D12 texture view requires a view type"
				);
			}

			d3d12::View result;
			result.base_mip_level = static_cast<std::uint32_t>(base_mip_lvl);
			result.mip_level_count = static_cast<std::uint32_t>(mip_lvl_cnt);
			result.base_array_layer = static_cast<std::uint32_t>(base_arr_layer);
			result.array_layer_count = static_cast<std::uint32_t>(arr_layer_cnt);
			result.format = format;
			if (is_srv) {
				D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {
					.Format = format,
					.ViewDimension = d3d12::SRVDimension(flags),
					.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING
				};
				switch (srv_desc.ViewDimension) {
				case D3D12_SRV_DIMENSION_TEXTURE1D:
					srv_desc.Texture1D.MostDetailedMip = static_cast<UINT>(base_mip_lvl);
					srv_desc.Texture1D.MipLevels = static_cast<UINT>(mip_lvl_cnt);
					srv_desc.Texture1D.ResourceMinLODClamp = 0.0f;
					break;
				case D3D12_SRV_DIMENSION_TEXTURE1DARRAY:
					srv_desc.Texture1DArray.MostDetailedMip = static_cast<UINT>(base_mip_lvl);
					srv_desc.Texture1DArray.MipLevels = static_cast<UINT>(mip_lvl_cnt);
					srv_desc.Texture1DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
					srv_desc.Texture1DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
					srv_desc.Texture1DArray.ResourceMinLODClamp = 0.0f;
					break;
				case D3D12_SRV_DIMENSION_TEXTURE2D:
					srv_desc.Texture2D.MostDetailedMip = static_cast<UINT>(base_mip_lvl);
					srv_desc.Texture2D.MipLevels = static_cast<UINT>(mip_lvl_cnt);
					srv_desc.Texture2D.PlaneSlice = 0;
					srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
					break;
				case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
					srv_desc.Texture2DArray.MostDetailedMip = static_cast<UINT>(base_mip_lvl);
					srv_desc.Texture2DArray.MipLevels = static_cast<UINT>(mip_lvl_cnt);
					srv_desc.Texture2DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
					srv_desc.Texture2DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
					srv_desc.Texture2DArray.PlaneSlice = 0;
					srv_desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
					break;
				case D3D12_SRV_DIMENSION_TEXTURE3D:
					srv_desc.Texture3D.MostDetailedMip = static_cast<UINT>(base_mip_lvl);
					srv_desc.Texture3D.MipLevels = static_cast<UINT>(mip_lvl_cnt);
					srv_desc.Texture3D.ResourceMinLODClamp = 0.0f;
					break;
				case D3D12_SRV_DIMENSION_TEXTURECUBE:
					srv_desc.TextureCube.MostDetailedMip = static_cast<UINT>(base_mip_lvl);
					srv_desc.TextureCube.MipLevels = static_cast<UINT>(mip_lvl_cnt);
					srv_desc.TextureCube.ResourceMinLODClamp = 0.0f;
					break;
				case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
					if (arr_layer_cnt == 0u || base_arr_layer % 6u != 0u || arr_layer_cnt % 6u != 0u) {
						throw std::invalid_argument(
							"A D3D12 cube array SRV requires base_array_layer and array_layer_count to be multiples of 6"
						);
					}
					srv_desc.TextureCubeArray.MostDetailedMip = static_cast<UINT>(base_mip_lvl);
					srv_desc.TextureCubeArray.MipLevels = static_cast<UINT>(mip_lvl_cnt);
					srv_desc.TextureCubeArray.First2DArrayFace = static_cast<UINT>(base_arr_layer);
					srv_desc.TextureCubeArray.NumCubes = static_cast<UINT>(arr_layer_cnt / 6u);
					srv_desc.TextureCubeArray.ResourceMinLODClamp = 0.0f;
					break;
				default:
					throw std::invalid_argument("Unsupported D3D12 SRV dimension");
				}
				auto handle = resource->resource_descriptors.Allocate();
				device->CreateShaderResourceView(tex, &srv_desc, handle.CPU());
				result.descriptors[static_cast<std::size_t>(Type::ShaderResource)] = std::move(handle);
			}

			if (is_uav) {
				D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
					.Format = format,
					.ViewDimension = d3d12::UAVDimension(flags)
				};
				switch (uav_desc.ViewDimension) {
				case D3D12_UAV_DIMENSION_TEXTURE1D:
					uav_desc.Texture1D.MipSlice = static_cast<UINT>(base_mip_lvl);
					break;
				case D3D12_UAV_DIMENSION_TEXTURE1DARRAY:
					uav_desc.Texture1DArray.MipSlice = static_cast<UINT>(base_mip_lvl);
					uav_desc.Texture1DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
					uav_desc.Texture1DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
					break;
				case D3D12_UAV_DIMENSION_TEXTURE2D:
					uav_desc.Texture2D.MipSlice = static_cast<UINT>(base_mip_lvl);
					uav_desc.Texture2D.PlaneSlice = 0;
					break;
				case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
					uav_desc.Texture2DArray.MipSlice = static_cast<UINT>(base_mip_lvl);
					uav_desc.Texture2DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
					uav_desc.Texture2DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
					uav_desc.Texture2DArray.PlaneSlice = 0;
					break;
				case D3D12_UAV_DIMENSION_TEXTURE3D:
					uav_desc.Texture3D.MipSlice = static_cast<UINT>(base_mip_lvl);
					uav_desc.Texture3D.FirstWSlice = static_cast<UINT>(base_arr_layer);
					uav_desc.Texture3D.WSize = static_cast<UINT>(arr_layer_cnt);
					break;
				default:
					throw std::invalid_argument("Unsupported D3D12 UAV dimension");
				}
				auto handle = resource->resource_descriptors.Allocate();
				device->CreateUnorderedAccessView(tex, nullptr, &uav_desc, handle.CPU());
				result.descriptors[static_cast<std::size_t>(Type::UnorderedAccess)] = std::move(handle);
			}

			if (is_rtv) {
				D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {
					.Format = format,
					.ViewDimension = d3d12::RTVDimension(flags)
				};
				switch (rtv_desc.ViewDimension) {
				case D3D12_RTV_DIMENSION_TEXTURE1D:
					rtv_desc.Texture1D.MipSlice = static_cast<UINT>(base_mip_lvl);
					break;
				case D3D12_RTV_DIMENSION_TEXTURE1DARRAY:
					rtv_desc.Texture1DArray.MipSlice = static_cast<UINT>(base_mip_lvl);
					rtv_desc.Texture1DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
					rtv_desc.Texture1DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
					break;
				case D3D12_RTV_DIMENSION_TEXTURE2D:
					rtv_desc.Texture2D.MipSlice = static_cast<UINT>(base_mip_lvl);
					rtv_desc.Texture2D.PlaneSlice = 0;
					break;
				case D3D12_RTV_DIMENSION_TEXTURE2DARRAY:
					rtv_desc.Texture2DArray.MipSlice = static_cast<UINT>(base_mip_lvl);
					rtv_desc.Texture2DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
					rtv_desc.Texture2DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
					rtv_desc.Texture2DArray.PlaneSlice = 0;
					break;
				case D3D12_RTV_DIMENSION_TEXTURE3D:
					rtv_desc.Texture3D.MipSlice = static_cast<UINT>(base_mip_lvl);
					rtv_desc.Texture3D.FirstWSlice = static_cast<UINT>(base_arr_layer);
					rtv_desc.Texture3D.WSize = static_cast<UINT>(arr_layer_cnt);
					break;
				default:
					throw std::invalid_argument("Unsupported D3D12 RTV dimension");
				}
				auto handle = resource->render_target_descriptors.Allocate();
				device->CreateRenderTargetView(tex, &rtv_desc, handle.CPU());
				result.descriptors[static_cast<std::size_t>(Type::RenderTarget)] = std::move(handle);
			}

			if (is_dsv) {
				D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {
					.Format = format,
					.ViewDimension = d3d12::DSVDimension(flags),
					.Flags = d3d12::DSVFlags(flags)
				};
				switch (dsv_desc.ViewDimension) {
				case D3D12_DSV_DIMENSION_TEXTURE1D:
					dsv_desc.Texture1D.MipSlice = static_cast<UINT>(base_mip_lvl);
					break;
				case D3D12_DSV_DIMENSION_TEXTURE1DARRAY:
					dsv_desc.Texture1DArray.MipSlice = static_cast<UINT>(base_mip_lvl);
					dsv_desc.Texture1DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
					dsv_desc.Texture1DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
					break;
				case D3D12_DSV_DIMENSION_TEXTURE2D:
					dsv_desc.Texture2D.MipSlice = static_cast<UINT>(base_mip_lvl);
					break;
				case D3D12_DSV_DIMENSION_TEXTURE2DARRAY:
					dsv_desc.Texture2DArray.MipSlice = static_cast<UINT>(base_mip_lvl);
					dsv_desc.Texture2DArray.FirstArraySlice = static_cast<UINT>(base_arr_layer);
					dsv_desc.Texture2DArray.ArraySize = static_cast<UINT>(arr_layer_cnt);
					break;
				default:
					throw std::invalid_argument("Unsupported D3D12 DSV dimension");
				}
				auto handle = resource->depth_stencil_descriptors.Allocate();
				device->CreateDepthStencilView(tex, &dsv_desc, handle.CPU());
				result.descriptors[static_cast<std::size_t>(Type::DepthStencil)] = std::move(handle);
			}

			return MakeView(std::move(result));
		}
	};

} // namespace fyuu_rhi
#endif // defined(_WIN32)
