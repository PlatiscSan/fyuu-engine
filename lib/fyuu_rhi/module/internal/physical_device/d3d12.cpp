module;
#if !defined(__EMSCRIPTEN__)
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <stdexcept>
#include <vector>

#include <array>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#include <D3D12MemAlloc.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#endif // defined(_WIN32)
#define BOOST_DISABLE_ASSERTS
#include <boost/locale.hpp>
#endif // !defined(__EMSCRIPTEN__)

module fyuu_rhi:d3d12_physical_device;
#if !defined(__EMSCRIPTEN__)
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :d3d12_data;
import :d3d12_descriptor_allocator;
import :d3d12_device_removal_tracker;
import :d3d12_utility;
import :logical_device_factory;
import :physical_device_dispatch;
import :memory_pressure;
import :physical_device_factory;
import :physical_device;
#endif // defined(_WIN32)

namespace {

	auto GetPhysicalDeviceType(DXGI_ADAPTER_DESC1 const& desc) {
		using Type = fyuu_rhi::PhysicalDevice::Info::Type;
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
			return Type::CPU;
		}
		if (desc.DedicatedVideoMemory > 0) {
			return Type::DiscreteGPU;
		}
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
		Microsoft::WRL::ComPtr<ID3D12CommandSignature> command_signature;
		std::array argument_descs = {
			D3D12_INDIRECT_ARGUMENT_DESC{ .Type = argument_type }
		};
		D3D12_COMMAND_SIGNATURE_DESC signature{
			.ByteStride = CommandSignatureStride(argument_type),
			.NumArgumentDescs = static_cast<UINT>(argument_descs.size()),
			.pArgumentDescs = argument_descs.data(),
			.NodeMask = 0u
		};
		fyuu_rhi::d3d12::ThrowIfFailed(
			device->CreateCommandSignature(&signature, nullptr, IID_PPV_ARGS(&command_signature))
		);
		return command_signature;
	}

}

namespace fyuu_rhi {

	template <>
	struct GetPhysicalDeviceInfo<d3d12::PhysicalDevice> {
		d3d12::PhysicalDevice const* native;
		PhysicalDevice::Info operator()() {

			DXGI_ADAPTER_DESC1 desc;
			HRESULT result = native->adapter->GetDesc1(&desc);
			d3d12::ThrowIfFailed(result);

			return {
				.name = boost::locale::conv::utf_to_utf<char>(desc.Description),
				.vendor_id = desc.VendorId,
				.device_id = desc.DeviceId,
				.dedicated_memory = desc.DedicatedVideoMemory,
				.type = GetPhysicalDeviceType(desc)
			};
		}
	};

	template <>
	struct CreateLogicalDevice<d3d12::PhysicalDevice> {
		d3d12::PhysicalDevice const* physical_device;

		LogicalDevice operator()() const {
			Microsoft::WRL::ComPtr<ID3D12Device> device;
			d3d12::ThrowIfFailed(D3D12CreateDevice(physical_device->adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));

			// Retains the adapter on the device so the logical device can recover it
			// later (e.g. pipeline creation queries the driver version from it).
			d3d12::ThrowIfFailed(device->SetPrivateDataInterface(__uuidof(IDXGIAdapter1), physical_device->adapter.Get()));

			d3d12::DeviceRemovalTracker rm_tracker(device);

			Microsoft::WRL::ComPtr<D3D12MA::Allocator> memory_allocator;
			D3D12MA::ALLOCATOR_DESC allocator_desc = {};
			allocator_desc.pDevice = device.Get();
			allocator_desc.pAdapter = physical_device->adapter.Get();
			/**
			 * @brief Limit default heap growth to 16 MiB rather than the 64 MiB library default.
			 * @note This is a block-size preference, not a total-memory cap. Large resources
			 * retain D3D12MA's committed fallback; tight alignment and explicit flags are unchanged.
			 */
			allocator_desc.PreferredBlockSize = 16ull * 1024ull * 1024ull;
			d3d12::ThrowIfFailed(D3D12MA::CreateAllocator(&allocator_desc, &memory_allocator));

			d3d12::DescriptorAllocator resource_descriptors(
				device,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
				65536u,
				true
			);
			d3d12::DescriptorAllocator render_target_descriptors(
				device,
				D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
				2048u,
				false
			);
			d3d12::DescriptorAllocator depth_stencil_descriptors(
				device,
				D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
				2048u,
				false
			);
			d3d12::DescriptorAllocator sampler_descriptors(
				device,
				D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
				2048u,
				true
			);

			auto multidraw = CreateCommandSignature(device.Get(), D3D12_INDIRECT_ARGUMENT_TYPE_DRAW);
			auto multidraw_indexed = CreateCommandSignature(device.Get(), D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED);
			auto dispatch_indirect = CreateCommandSignature(device.Get(), D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH);
			/// @brief CopyDescriptorsSimple sources must never reside in shader-visible heaps.
			d3d12::DescriptorAllocator resource_descriptor_storage(
				device,
				D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
				65536u,
				false
			);
			d3d12::DescriptorAllocator sampler_descriptor_storage(
				device,
				D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
				2048u,
				false
			);

			return MakeLogicalDevice(
				d3d12::LogicalDevice{
					std::move(rm_tracker),
					std::move(memory_allocator),
					std::move(resource_descriptors),
					std::move(render_target_descriptors),
					std::move(depth_stencil_descriptors),
					std::move(sampler_descriptors),
					std::move(multidraw),
					std::move(multidraw_indexed),
					std::move(dispatch_indirect),
					std::move(resource_descriptor_storage),
					std::move(sampler_descriptor_storage)
				}
			);
		}
	};

}
#endif // !defined(__EMSCRIPTEN__)
