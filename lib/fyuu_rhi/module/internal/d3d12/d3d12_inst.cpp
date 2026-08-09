module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <vector>

#include <string_view>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <dxgi1_3.h>
#include <d3d12.h>
#include <wrl.h>
#endif // defined(_WIN32)
module fyuu_rhi:d3d12_instance;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :d3d12_traits;
import :d3d12_utility;
import :cache_system;

namespace fyuu_rhi::d3d12 {

	Microsoft::WRL::ComPtr<IDXGIFactory2> Backend::CreateInstance(std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver) {
		Microsoft::WRL::ComPtr<IDXGIFactory2> factory;

		UINT create_factory_flags =
#if defined(NDEBUG)
			0;
#else
			// Request a debug factory if we are in a debug build.
			// This allows additional validation in DXGI itself.
			DXGI_CREATE_FACTORY_DEBUG;
#endif
		HRESULT result = CreateDXGIFactory2(create_factory_flags, IID_PPV_ARGS(&factory));
		ThrowIfFailed(result);
#if !defined(NDEBUG)   // Only enable debug features in non‑release builds.
		// The debug layer must be enabled before creating any D3D12 device.
		// If we enable it after device creation, the runtime would discard the device.
		Microsoft::WRL::ComPtr<ID3D12Debug> debug_interface;
		result = D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface));
		ThrowIfFailed(result);
		debug_interface->EnableDebugLayer();

		// GPU‑based validation helps catch errors that occur during shader execution.
		Microsoft::WRL::ComPtr<ID3D12Debug1> debug_controller1;
		result = debug_interface->QueryInterface(IID_PPV_ARGS(&debug_controller1));
		ThrowIfFailed(result);
		debug_controller1->SetEnableGPUBasedValidation(true);

		// Enable Device Removed Extended Data (DRED) to get better diagnostics
		// when a device removal happens (e.g., TDR, page faults).
		Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedDataSettings> dred_settings;
		result = D3D12GetDebugInterface(IID_PPV_ARGS(&dred_settings));
		ThrowIfFailed(result);

		// Force‑enable auto‑breadcrumbs (record of GPU commands) and page fault reporting.
		dred_settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
		dred_settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
#endif // !defined(NDEBUG)

		cache::Initialize(app_name, app_ver, engine_name, engine_ver);

		return factory;

	}

	std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter1>> Backend::EnumeratePhysicalDevices(Microsoft::WRL::ComPtr<IDXGIFactory2> const& factory) {
		std::vector<Microsoft::WRL::ComPtr<IDXGIAdapter1>> adapters;

		Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;

		for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
			adapters.emplace_back(std::move(adapter));
		}

		return adapters;
	}

}
#endif // defined(_WIN32)