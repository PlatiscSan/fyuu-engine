/* d3d12_utility.cppm */
module;
#include <version>
#if !defined(__cpp_lib_modules)

#include <memory>
#include <utility>
#include <vector>

#include <cstdint>
#include <type_traits>
#include <array>
#include <system_error>

#include <memory_resource>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#include <comdef.h>
#include <wil/resource.h>
#endif // defined(_WIN32)
#define BOOST_DISABLE_ASSERTS
#include <boost/locale.hpp>
module fyuu_rhi:d3d12_utility;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :resource;
import :sampler;

namespace {
	thread_local std::vector<wil::unique_event> s_events;
}

namespace fyuu_rhi::d3d12 {

	DXGI_FORMAT ResourceFormat(fyuu_rhi::ResourceFlags const& flags, bool required) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		static constexpr std::array formats{
			std::pair{ Bits::R8Unorm, DXGI_FORMAT_R8_UNORM },
			std::pair{ Bits::R8Snorm, DXGI_FORMAT_R8_SNORM },
			std::pair{ Bits::R8Uint, DXGI_FORMAT_R8_UINT },
			std::pair{ Bits::R8Sint, DXGI_FORMAT_R8_SINT },
			std::pair{ Bits::R8G8Unorm, DXGI_FORMAT_R8G8_UNORM },
			std::pair{ Bits::R8G8Snorm, DXGI_FORMAT_R8G8_SNORM },
			std::pair{ Bits::R8G8Uint, DXGI_FORMAT_R8G8_UINT },
			std::pair{ Bits::R8G8Sint, DXGI_FORMAT_R8G8_SINT },
			std::pair{ Bits::R8G8B8A8Unorm, DXGI_FORMAT_R8G8B8A8_UNORM },
			std::pair{ Bits::R8G8B8A8Snorm, DXGI_FORMAT_R8G8B8A8_SNORM },
			std::pair{ Bits::R8G8B8A8Uint, DXGI_FORMAT_R8G8B8A8_UINT },
			std::pair{ Bits::R8G8B8A8Sint, DXGI_FORMAT_R8G8B8A8_SINT },
			std::pair{ Bits::R8G8B8A8Srgb, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB },
			std::pair{ Bits::B8G8R8A8Srgb, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB },
			std::pair{ Bits::R16Unorm, DXGI_FORMAT_R16_UNORM },
			std::pair{ Bits::R16Snorm, DXGI_FORMAT_R16_SNORM },
			std::pair{ Bits::R16Uint, DXGI_FORMAT_R16_UINT },
			std::pair{ Bits::R16Sint, DXGI_FORMAT_R16_SINT },
			std::pair{ Bits::R16Float, DXGI_FORMAT_R16_FLOAT },
			std::pair{ Bits::R16G16Unorm, DXGI_FORMAT_R16G16_UNORM },
			std::pair{ Bits::R16G16Snorm, DXGI_FORMAT_R16G16_SNORM },
			std::pair{ Bits::R16G16Uint, DXGI_FORMAT_R16G16_UINT },
			std::pair{ Bits::R16G16Sint, DXGI_FORMAT_R16G16_SINT },
			std::pair{ Bits::R16G16Float, DXGI_FORMAT_R16G16_FLOAT },
			std::pair{ Bits::R16G16B16A16Unorm, DXGI_FORMAT_R16G16B16A16_UNORM },
			std::pair{ Bits::R16G16B16A16Snorm, DXGI_FORMAT_R16G16B16A16_SNORM },
			std::pair{ Bits::R16G16B16A16Uint, DXGI_FORMAT_R16G16B16A16_UINT },
			std::pair{ Bits::R16G16B16A16Sint, DXGI_FORMAT_R16G16B16A16_SINT },
			std::pair{ Bits::R16G16B16A16Float, DXGI_FORMAT_R16G16B16A16_FLOAT },
			std::pair{ Bits::R32Uint, DXGI_FORMAT_R32_UINT },
			std::pair{ Bits::R32Sint, DXGI_FORMAT_R32_SINT },
			std::pair{ Bits::R32Float, DXGI_FORMAT_R32_FLOAT },
			std::pair{ Bits::R32G32Uint, DXGI_FORMAT_R32G32_UINT },
			std::pair{ Bits::R32G32Sint, DXGI_FORMAT_R32G32_SINT },
			std::pair{ Bits::R32G32Float, DXGI_FORMAT_R32G32_FLOAT },
			std::pair{ Bits::R32G32B32A32Uint, DXGI_FORMAT_R32G32B32A32_UINT },
			std::pair{ Bits::R32G32B32A32Sint, DXGI_FORMAT_R32G32B32A32_SINT },
			std::pair{ Bits::R32G32B32A32Float, DXGI_FORMAT_R32G32B32A32_FLOAT },
			std::pair{ Bits::R10G10B10A2Unorm, DXGI_FORMAT_R10G10B10A2_UNORM },
			std::pair{ Bits::R10G10B10A2Uint, DXGI_FORMAT_R10G10B10A2_UINT },
			std::pair{ Bits::R11G11B10Float, DXGI_FORMAT_R11G11B10_FLOAT },
			std::pair{ Bits::R9G9B9E5SharedExp, DXGI_FORMAT_R9G9B9E5_SHAREDEXP },
			std::pair{ Bits::D16Unorm, DXGI_FORMAT_D16_UNORM },
			std::pair{ Bits::D24UnormS8Uint, DXGI_FORMAT_D24_UNORM_S8_UINT },
			std::pair{ Bits::D32Float, DXGI_FORMAT_D32_FLOAT },
			std::pair{ Bits::D32FloatS8X24Uint, DXGI_FORMAT_D32_FLOAT_S8X24_UINT },
			std::pair{ Bits::Bc1Unorm, DXGI_FORMAT_BC1_UNORM },
			std::pair{ Bits::Bc1UnormSrgb, DXGI_FORMAT_BC1_UNORM_SRGB },
			std::pair{ Bits::Bc2Unorm, DXGI_FORMAT_BC2_UNORM },
			std::pair{ Bits::Bc2UnormSrgb, DXGI_FORMAT_BC2_UNORM_SRGB },
			std::pair{ Bits::Bc3Unorm, DXGI_FORMAT_BC3_UNORM },
			std::pair{ Bits::Bc3UnormSrgb, DXGI_FORMAT_BC3_UNORM_SRGB },
			std::pair{ Bits::Bc4Unorm, DXGI_FORMAT_BC4_UNORM },
			std::pair{ Bits::Bc4Snorm, DXGI_FORMAT_BC4_SNORM },
			std::pair{ Bits::Bc5Unorm, DXGI_FORMAT_BC5_UNORM },
			std::pair{ Bits::Bc5Snorm, DXGI_FORMAT_BC5_SNORM },
			std::pair{ Bits::Bc6HUfloat, DXGI_FORMAT_BC6H_UF16 },
			std::pair{ Bits::Bc6HSfloat, DXGI_FORMAT_BC6H_SF16 },
			std::pair{ Bits::Bc7Unorm, DXGI_FORMAT_BC7_UNORM },
			std::pair{ Bits::Bc7UnormSrgb, DXGI_FORMAT_BC7_UNORM_SRGB }
		};
		DXGI_FORMAT result = DXGI_FORMAT_UNKNOWN;
		for (auto const& [flag, format] : formats) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (result != DXGI_FORMAT_UNKNOWN) {
				throw std::invalid_argument("A D3D12 texture must specify at most one format");
			}
			result = format;
		}
		if (required && result == DXGI_FORMAT_UNKNOWN) {
			throw std::invalid_argument("A D3D12 texture requires a format");
		}
		return result;
	}

	UINT SampleCount(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		static constexpr std::array counts{
			std::pair{ Bits::Sample1, 1u },
			std::pair{ Bits::Sample2, 2u },
			std::pair{ Bits::Sample4, 4u },
			std::pair{ Bits::Sample8, 8u },
			std::pair{ Bits::Sample16, 16u },
			std::pair{ Bits::Sample32, 32u },
			std::pair{ Bits::Sample64, 64u }
		};
		UINT result = 1u;
		bool found = false;
		for (auto const& [flag, count] : counts) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (found) {
				throw std::invalid_argument(
					"A D3D12 texture must specify at most one sample count"
				);
			}
			found = true;
			result = count;
		}
		return result;
	}

	bool IsDepthStencil(DXGI_FORMAT format) noexcept {
		return format == DXGI_FORMAT_D16_UNORM ||
			format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
			format == DXGI_FORMAT_D32_FLOAT ||
			format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	}

	std::size_t FormatSize(DXGI_FORMAT format) noexcept {
		switch (format) {
		case DXGI_FORMAT_R8_UNORM:
		case DXGI_FORMAT_R8_SNORM:
		case DXGI_FORMAT_R8_UINT:
		case DXGI_FORMAT_R8_SINT:
			return 1u;
		case DXGI_FORMAT_R8G8_UNORM:
		case DXGI_FORMAT_R8G8_SNORM:
		case DXGI_FORMAT_R8G8_UINT:
		case DXGI_FORMAT_R8G8_SINT:
		case DXGI_FORMAT_R16_UNORM:
		case DXGI_FORMAT_R16_SNORM:
		case DXGI_FORMAT_R16_UINT:
		case DXGI_FORMAT_R16_SINT:
		case DXGI_FORMAT_R16_FLOAT:
			return 2u;
		case DXGI_FORMAT_R8G8B8A8_UNORM:
		case DXGI_FORMAT_R8G8B8A8_SNORM:
		case DXGI_FORMAT_R8G8B8A8_UINT:
		case DXGI_FORMAT_R8G8B8A8_SINT:
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		case DXGI_FORMAT_R16G16_UNORM:
		case DXGI_FORMAT_R16G16_SNORM:
		case DXGI_FORMAT_R16G16_UINT:
		case DXGI_FORMAT_R16G16_SINT:
		case DXGI_FORMAT_R16G16_FLOAT:
		case DXGI_FORMAT_R32_UINT:
		case DXGI_FORMAT_R32_SINT:
		case DXGI_FORMAT_R32_FLOAT:
		case DXGI_FORMAT_R10G10B10A2_UNORM:
		case DXGI_FORMAT_R10G10B10A2_UINT:
		case DXGI_FORMAT_R11G11B10_FLOAT:
		case DXGI_FORMAT_R9G9B9E5_SHAREDEXP:
			return 4u;
		case DXGI_FORMAT_R16G16B16A16_UNORM:
		case DXGI_FORMAT_R16G16B16A16_SNORM:
		case DXGI_FORMAT_R16G16B16A16_UINT:
		case DXGI_FORMAT_R16G16B16A16_SINT:
		case DXGI_FORMAT_R16G16B16A16_FLOAT:
		case DXGI_FORMAT_R32G32_UINT:
		case DXGI_FORMAT_R32G32_SINT:
		case DXGI_FORMAT_R32G32_FLOAT:
			return 8u;
		case DXGI_FORMAT_R32G32B32A32_UINT:
		case DXGI_FORMAT_R32G32B32A32_SINT:
		case DXGI_FORMAT_R32G32B32A32_FLOAT:
			return 16u;
		default:
			return 0u;
		}
	}

	D3D12_SRV_DIMENSION SRVDimension(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		static constexpr std::array dimensions{
			std::pair{ Bits::TextureView1D, D3D12_SRV_DIMENSION_TEXTURE1D },
			std::pair{ Bits::TextureView2D, D3D12_SRV_DIMENSION_TEXTURE2D },
			std::pair{ Bits::TextureView2DArray, D3D12_SRV_DIMENSION_TEXTURE2DARRAY },
			std::pair{ Bits::TextureView3D, D3D12_SRV_DIMENSION_TEXTURE3D },
			std::pair{ Bits::TextureViewCube, D3D12_SRV_DIMENSION_TEXTURECUBE },
			std::pair{ Bits::TextureViewCubeArray, D3D12_SRV_DIMENSION_TEXTURECUBEARRAY }
		};
		D3D12_SRV_DIMENSION result = D3D12_SRV_DIMENSION_TEXTURE2D;
		bool found = false;
		for (auto const& [flag, dimension] : dimensions) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (found) {
				throw std::invalid_argument(
					"A D3D12 SRV requires exactly one texture view type"
				);
			}
			found = true;
			result = dimension;
		}
		return result;
	}

	D3D12_UAV_DIMENSION UAVDimension(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		static constexpr std::array dimensions{
			std::pair{ Bits::TextureView1D, D3D12_UAV_DIMENSION_TEXTURE1D },
			std::pair{ Bits::TextureView2D, D3D12_UAV_DIMENSION_TEXTURE2D },
			std::pair{ Bits::TextureView2DArray, D3D12_UAV_DIMENSION_TEXTURE2DARRAY },
			std::pair{ Bits::TextureView3D, D3D12_UAV_DIMENSION_TEXTURE3D }
		};
		D3D12_UAV_DIMENSION result = D3D12_UAV_DIMENSION_TEXTURE2D;
		bool found = false;
		for (auto const& [flag, dimension] : dimensions) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (found) {
				throw std::invalid_argument(
					"A D3D12 UAV requires exactly one texture view type"
				);
			}
			found = true;
			result = dimension;
		}
		return result;
	}

	D3D12_RTV_DIMENSION RTVDimension(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		static constexpr std::array dimensions{
			std::pair{ Bits::TextureView1D, D3D12_RTV_DIMENSION_TEXTURE1D },
			std::pair{ Bits::TextureView2D, D3D12_RTV_DIMENSION_TEXTURE2D },
			std::pair{ Bits::TextureView2DArray, D3D12_RTV_DIMENSION_TEXTURE2DARRAY },
			std::pair{ Bits::TextureView3D, D3D12_RTV_DIMENSION_TEXTURE3D }
		};
		D3D12_RTV_DIMENSION result = D3D12_RTV_DIMENSION_TEXTURE2D;
		bool found = false;
		for (auto const& [flag, dimension] : dimensions) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (found) {
				throw std::invalid_argument(
					"A D3D12 RTV requires exactly one texture view type"
				);
			}
			found = true;
			result = dimension;
		}
		return result;
	}

	D3D12_DSV_DIMENSION DSVDimension(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		static constexpr std::array dimensions{
			std::pair{ Bits::TextureView1D, D3D12_DSV_DIMENSION_TEXTURE1D },
			std::pair{ Bits::TextureView2D, D3D12_DSV_DIMENSION_TEXTURE2D },
			std::pair{ Bits::TextureView2DArray, D3D12_DSV_DIMENSION_TEXTURE2DARRAY }
		};
		D3D12_DSV_DIMENSION result = D3D12_DSV_DIMENSION_TEXTURE2D;
		bool found = false;
		for (auto const& [flag, dimension] : dimensions) {
			if (!flags.Test(flag)) {
				continue;
			}
			if (found) {
				throw std::invalid_argument(
					"A D3D12 DSV requires exactly one texture view type"
				);
			}
			found = true;
			result = dimension;
		}
		return result;
	}

	D3D12_DSV_FLAGS DSVFlags(fyuu_rhi::ResourceFlags const& flags) noexcept {
		using Bits = fyuu_rhi::ResourceFlagBits;
		D3D12_DSV_FLAGS result = D3D12_DSV_FLAG_NONE;
		if (flags.Test(Bits::TextureViewAspectDepthOnly) &&
			!flags.Test(Bits::TextureViewAspectStencilOnly)) {
			result |= D3D12_DSV_FLAG_READ_ONLY_DEPTH;
		}
		if (flags.Test(Bits::TextureViewAspectStencilOnly) &&
			!flags.Test(Bits::TextureViewAspectDepthOnly)) {
			result |= D3D12_DSV_FLAG_READ_ONLY_STENCIL;
		}
		return result;
	}

	D3D12_TEXTURE_ADDRESS_MODE SamplerAddressMode(AddressMode mode) noexcept {
		switch (mode) {
		case AddressMode::ClampToEdge:
			return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		case AddressMode::Repeat:
			return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		case AddressMode::MirroredRepeat:
			return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
		default:
			return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		}
	}

	D3D12_COMPARISON_FUNC ComparisonOperation(CompareFunction func) noexcept {
		switch (func) {
		case CompareFunction::Never:
			return D3D12_COMPARISON_FUNC_NEVER;
		case CompareFunction::Less:
			return D3D12_COMPARISON_FUNC_LESS;
		case CompareFunction::Equal:
			return D3D12_COMPARISON_FUNC_EQUAL;
		case CompareFunction::LessEqual:
			return D3D12_COMPARISON_FUNC_LESS_EQUAL;
		case CompareFunction::Greater:
			return D3D12_COMPARISON_FUNC_GREATER;
		case CompareFunction::NotEqual:
			return D3D12_COMPARISON_FUNC_NOT_EQUAL;
		case CompareFunction::GreaterEqual:
			return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
		case CompareFunction::Always:
			return D3D12_COMPARISON_FUNC_ALWAYS;
		default:
			return D3D12_COMPARISON_FUNC_NONE;
		}
	}

	D3D12_FILTER_TYPE FilterType(FilterMode mode) noexcept {
		return mode == FilterMode::Nearest ?
			D3D12_FILTER_TYPE_POINT :
			D3D12_FILTER_TYPE_LINEAR;
	}

	D3D12_FILTER_TYPE MipmapFilterType(MipmapFilterMode mode) noexcept {
		return mode == MipmapFilterMode::Nearest ?
			D3D12_FILTER_TYPE_POINT :
			D3D12_FILTER_TYPE_LINEAR;
	}

	D3D12_FILTER BuildFilter(
		FilterMode mag,
		FilterMode min,
		MipmapFilterMode mip,
		UINT max_anisotropy,
		CompareFunction compare
	) noexcept {
		bool anisotropic = max_anisotropy > 1u;
		bool comparison = compare != CompareFunction::Unknown;
		if (anisotropic) {
			return comparison ?
				D3D12_FILTER_COMPARISON_ANISOTROPIC :
				D3D12_FILTER_ANISOTROPIC;
		}
		auto mag_type = FilterType(mag);
		auto min_type = FilterType(min);
		auto mip_type = MipmapFilterType(mip);
		auto reduction = comparison ?
			D3D12_FILTER_REDUCTION_TYPE_COMPARISON :
			D3D12_FILTER_REDUCTION_TYPE_STANDARD;
		return static_cast<D3D12_FILTER>(
			D3D12_ENCODE_BASIC_FILTER(min_type, mag_type, mip_type, reduction)
		);
	}

	struct ManagedEvent {
		wil::unique_event impl;

		explicit ManagedEvent(wil::unique_event&& event) noexcept
			: impl(std::move(event)) {

		}

		ManagedEvent(ManagedEvent const&) = delete;
		ManagedEvent& operator=(ManagedEvent const&) = delete;
		ManagedEvent(ManagedEvent&&) noexcept = default;
		ManagedEvent& operator=(ManagedEvent&&) noexcept = default;

		~ManagedEvent() noexcept {
			if (impl) {
				s_events.emplace_back(std::move(impl));
			}
		}
	};

	void ThrowIfFailed(HRESULT result) {

		if (!FAILED(result)) {
			return;
		}

		_com_error error(result);

		std::string message = boost::locale::conv::utf_to_utf<char>(error.ErrorMessage());

		throw std::system_error(std::error_code(result, std::system_category()), message);
		
	}


	ManagedEvent CreateManagedEvent() {
		if (s_events.empty()) {
			return ManagedEvent(wil::unique_event(wil::EventOptions::None));
		}

		wil::unique_event event = std::move(s_events.back());
		s_events.pop_back();
		event.ResetEvent();

		return ManagedEvent(std::move(event));

	}

	/// Blocks without polling until the queue timeline reaches value.
	void WaitForFence(Microsoft::WRL::ComPtr<ID3D12Fence> const& fence, std::uint64_t value) {
		if (fence->GetCompletedValue() >= value) {
			return;
		}
		auto event = CreateManagedEvent();
		ThrowIfFailed(fence->SetEventOnCompletion(value, event.impl.get()));
		event.impl.wait();
	}

	/// Recovers the device that owns a native queue without exposing a borrowed pointer.
	Microsoft::WRL::ComPtr<ID3D12Device> GetLogicalDevice(Microsoft::WRL::ComPtr<ID3D12CommandQueue> const& queue) {
		Microsoft::WRL::ComPtr<ID3D12Device> result;
		ThrowIfFailed(queue->GetDevice(IID_PPV_ARGS(&result)));
		return result;
	}

	/// Recovers the adapter retained on the device by CreateLogicalDevice.
	Microsoft::WRL::ComPtr<IDXGIAdapter1> GetPhysicalDevice(Microsoft::WRL::ComPtr<ID3D12Device> const& device) {
		UINT size = sizeof(IDXGIAdapter1*);
		IDXGIAdapter1* adapter = nullptr;
		ThrowIfFailed(device->GetPrivateData(__uuidof(IDXGIAdapter1), &size, &adapter));
		// GetPrivateData returns an owned interface reference, so Attach consumes it
		// without performing a second AddRef.
		Microsoft::WRL::ComPtr<IDXGIAdapter1> result;
		result.Attach(adapter);
		return result;
	}

	/// Walks from an adapter to the factory that enumerated it.
	Microsoft::WRL::ComPtr<IDXGIFactory2> GetInstance(Microsoft::WRL::ComPtr<IDXGIAdapter1> const& adapter) {
		Microsoft::WRL::ComPtr<IDXGIFactory2> result;
		ThrowIfFailed(adapter->GetParent(IID_PPV_ARGS(&result)));
		return result;
	}

	/// Queries optional tearing support; unsupported interfaces and failed queries are false.
	bool IsTearingSupported(Microsoft::WRL::ComPtr<IDXGIFactory2> const& factory) noexcept {
		Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
		if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&factory5)))) {
			return false;
		}
		BOOL supported = FALSE;
		if (FAILED(
			factory5->CheckFeatureSupport(
				DXGI_FEATURE_PRESENT_ALLOW_TEARING,
				&supported,
				sizeof(supported)
			)
		)) {
			return false;
		}
		return supported == TRUE;
	}

}
#endif // defined(_WIN32)
