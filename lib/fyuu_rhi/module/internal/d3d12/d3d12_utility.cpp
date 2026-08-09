/* d3d12_utility.cppm */
module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>

#include <vector>

#include <cstdint>
#include <type_traits>

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

namespace {
	thread_local std::vector<wil::unique_event> s_events;
}

namespace fyuu_rhi::d3d12 {

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
		ThrowIfFailed(
			queue->GetDevice(IID_PPV_ARGS(&result))
		);
		return result;
	}

	/// Recovers the adapter retained on the device by CreateLogicalDevice.
	Microsoft::WRL::ComPtr<IDXGIAdapter1> GetPhysicalDevice(Microsoft::WRL::ComPtr<ID3D12Device> const& device) {
		UINT size = sizeof(IDXGIAdapter1*);
		IDXGIAdapter1* adapter = nullptr;
		ThrowIfFailed(
			device->GetPrivateData(__uuidof(IDXGIAdapter1), &size, &adapter)
		);
		// GetPrivateData returns an owned interface reference, so Attach consumes it
		// without performing a second AddRef.
		Microsoft::WRL::ComPtr<IDXGIAdapter1> result;
		result.Attach(adapter);
		return result;
	}

	/// Walks from an adapter to the factory that enumerated it.
	Microsoft::WRL::ComPtr<IDXGIFactory2> GetInstance(Microsoft::WRL::ComPtr<IDXGIAdapter1> const& adapter) {
		Microsoft::WRL::ComPtr<IDXGIFactory2> result;
		ThrowIfFailed(
			adapter->GetParent(IID_PPV_ARGS(&result))
		);
		return result;
	}

	/// Queries optional tearing support; unsupported interfaces and failed queries are false.
	bool TearingSupported(Microsoft::WRL::ComPtr<IDXGIFactory2> const& factory) noexcept {
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
