/* d3d12_instance.cpp */
module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <vector>
#include <functional>

#include <span>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>
#endif // defined(_WIN32)

module fyuu_rhi:d3d12_instance;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :instance_dispatch;
import :instance_factory;
import :d3d12_data;
import :d3d12_utility;
import :physical_device_factory;

namespace fyuu_rhi {

	template <>
	struct CreateInstance<d3d12::Instance> {
		d3d12::Instance operator()() const {
			Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
			HRESULT result = CreateDXGIFactory2(
#if defined(NDEBUG)
				0
#else
				// Request a debug factory if we are in a debug build.
				// This allows additional validation in DXGI itself.
				DXGI_CREATE_FACTORY_DEBUG
#endif
				, IID_PPV_ARGS(&factory)
			);
			d3d12::ThrowIfFailed(result);
#if !defined(NDEBUG)   // Only enable debug features in non‑release builds.
			// The debug layer must be enabled before creating any D3D12 device.
			// If we enable it after device creation, the runtime would discard the device.
			Microsoft::WRL::ComPtr<ID3D12Debug> debug_interface;
			result = D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface));
			d3d12::ThrowIfFailed(result);
			debug_interface->EnableDebugLayer();
	
			// GPU‑based validation helps catch errors that occur during shader execution.
			Microsoft::WRL::ComPtr<ID3D12Debug1> debug_controller1;
			result = debug_interface->QueryInterface(IID_PPV_ARGS(&debug_controller1));
			d3d12::ThrowIfFailed(result);
			debug_controller1->SetEnableGPUBasedValidation(true);
	
			// Enable Device Removed Extended Data (DRED) to get better diagnostics
			// when a device removal happens (e.g., TDR, page faults).
			Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred_settings;
			result = D3D12GetDebugInterface(IID_PPV_ARGS(&dred_settings));
			d3d12::ThrowIfFailed(result);
	
			// Force‑enable auto‑breadcrumbs (record of GPU commands) and page fault reporting.
			dred_settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
			dred_settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
#endif // !defined(NDEBUG)
			return { factory };
		}
	};

	template <>
	struct EnumeratePhysicalDevices<d3d12::Instance> {
		d3d12::Instance const* instance;

		std::vector<PhysicalDevice> operator()() const {
			std::vector<PhysicalDevice> physical_devices;
			Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
			for (UINT index = 0u; instance->factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
				if (adapter) {
					physical_devices.emplace_back(MakePhysicalDevice(d3d12::PhysicalDevice{ std::move(adapter) }));
				}
			}
			return physical_devices;
		}
	};

}

#endif // defined(_WIN32)
