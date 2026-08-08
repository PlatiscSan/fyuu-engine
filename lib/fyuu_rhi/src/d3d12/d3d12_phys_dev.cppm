module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <array>
#include <stdexcept>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <dxgi1_3.h>
#include <D3D12MemAlloc.h>
#include <wrl.h>
#include <comdef.h>
#endif // defined(_WIN32)
#define BOOST_DISABLE_ASSERTS
#include <boost/locale.hpp>
module fyuu_rhi:d3d12_physical_device;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :d3d12_traits;
import :d3d12_utility;

namespace {

	using namespace fyuu_rhi::d3d12;

	fyuu_rhi::PhysicalDeviceInfo::Type GetPhysicalDeviceType(DXGI_ADAPTER_DESC1 const& desc) {
		using Type = fyuu_rhi::PhysicalDeviceInfo::Type;
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) return Type::CPU;
		if (desc.DedicatedVideoMemory > 0) return Type::DiscreteGPU;
		return Type::IntegratedGPU;
	}

	UINT CommandSignatureStride(D3D12_INDIRECT_ARGUMENT_TYPE argument_type) {
		switch (argument_type) {
		case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW:
			return sizeof(D3D12_DRAW_ARGUMENTS);
		case D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED:
			return sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
		case D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH:
			return sizeof(D3D12_DISPATCH_ARGUMENTS);
		default:
			throw std::invalid_argument("Unsupported D3D12 command signature argument type");
		}
	}

	Microsoft::WRL::ComPtr<ID3D12CommandSignature> CreateCommandSignature(
		ID3D12Device* device,
		D3D12_INDIRECT_ARGUMENT_TYPE argument_type
	) {
		Microsoft::WRL::ComPtr<ID3D12CommandSignature> cmd_sgn;
		std::array argument_descs = {
			D3D12_INDIRECT_ARGUMENT_DESC{.Type = argument_type }
		};
		D3D12_COMMAND_SIGNATURE_DESC signature{
			.ByteStride = CommandSignatureStride(argument_type),
			.NumArgumentDescs = static_cast<UINT>(argument_descs.size()),
			.pArgumentDescs = argument_descs.data(),
			.NodeMask = 0u
		};
		HRESULT result = device->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&cmd_sgn));
		ThrowIfFailed(result);
		return cmd_sgn;
	}
}

namespace fyuu_rhi::d3d12 {

	PhysicalDeviceInfo Backend::GetPhysicalDeviceInfo(Microsoft::WRL::ComPtr<IDXGIAdapter1> const& adapter) {

		DXGI_ADAPTER_DESC1 desc;
		HRESULT result = adapter->GetDesc1(&desc);
		ThrowIfFailed(result);

		PhysicalDeviceInfo info = {
			.name = boost::locale::conv::utf_to_utf<char>(desc.Description),
			.vendor_id = desc.VendorId,
			.device_id = desc.DeviceId,
			.dedicated_memory = desc.DedicatedVideoMemory,
			.type = GetPhysicalDeviceType(desc)
		};

		return info;
	}

	Backend::LogicalDevice Backend::CreateLogicalDevice(Microsoft::WRL::ComPtr<IDXGIAdapter1> const& adapter) {

		Microsoft::WRL::ComPtr<ID3D12Device> dev;
		HRESULT result = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&dev));
		ThrowIfFailed(result);

		DeviceRemovalTracker rm_tracker(dev);

		Microsoft::WRL::ComPtr<D3D12MA::Allocator> mem_alloc;
		D3D12MA::ALLOCATOR_DESC allocator_desc = {};
		allocator_desc.pDevice = dev.Get();
		allocator_desc.pAdapter = adapter.Get();
		result = D3D12MA::CreateAllocator(&allocator_desc, &mem_alloc);
		ThrowIfFailed(result);

		DescriptorAllocator univ_alloc = CreateUniversalViewAllocator(dev);
		DescriptorAllocator rtv_alloc = CreateRTVAllocator(dev);
		DescriptorAllocator dsv_alloc = CreateDSVAllocator(dev);
		DescriptorAllocator sampler_alloc = CreateSamplerAllocator(dev);

		Microsoft::WRL::ComPtr<ID3D12CommandSignature> multidraw = CreateCommandSignature(dev.Get(), D3D12_INDIRECT_ARGUMENT_TYPE_DRAW);
		Microsoft::WRL::ComPtr<ID3D12CommandSignature> multidraw_indexed = CreateCommandSignature(dev.Get(), D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED);
		Microsoft::WRL::ComPtr<ID3D12CommandSignature> dispatch_indirect = CreateCommandSignature(dev.Get(), D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH);

		result = dev->SetPrivateDataInterface(__uuidof(IDXGIAdapter1), adapter.Get());
		ThrowIfFailed(result);

		return { 
			std::move(dev), 
			std::move(rm_tracker), 
			std::move(mem_alloc), 
			std::move(univ_alloc), 
			std::move(rtv_alloc), 
			std::move(dsv_alloc), 
			std::move(sampler_alloc),
			std::move(multidraw),
			std::move(multidraw_indexed),
			std::move(dispatch_indirect)
		};

	}

}
#endif // defined(_WIN32)
