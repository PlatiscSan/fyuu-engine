module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <limits>
#include <cstdint>

#include <string_view>

#include <format>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <d3d12.h>
#include <wrl.h>
#include <wil/resource.h>
#endif // defined(_WIN32)

module fyuu_rhi:d3d12_device_removal_tracker;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :log;
import :d3d12_utility;

namespace {

	void LogD3D12Message(
		D3D12_MESSAGE_CATEGORY category,
		D3D12_MESSAGE_SEVERITY severity,
		D3D12_MESSAGE_ID identifier,
		LPCSTR description,
		void*
	) {
		auto message = std::format(
			"D3D12[category={}, id={}]: {}",
			static_cast<std::uint32_t>(category),
			static_cast<std::uint32_t>(identifier),
			description ? description : ""
		);
		switch (severity) {
		case D3D12_MESSAGE_SEVERITY_CORRUPTION:
			fyuu_rhi::log::Fatal(message);
			break;
		case D3D12_MESSAGE_SEVERITY_ERROR:
			fyuu_rhi::log::Error(message);
			break;
		case D3D12_MESSAGE_SEVERITY_WARNING:
			fyuu_rhi::log::Warning(message);
			break;
		case D3D12_MESSAGE_SEVERITY_INFO:
			fyuu_rhi::log::Info(message);
			break;
		case D3D12_MESSAGE_SEVERITY_MESSAGE:
			fyuu_rhi::log::Debug(message);
			break;
		}
	}

	void LogBreadcrumbs(ID3D12DeviceRemovedExtendedData* dred) noexcept {
		D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT output{};
		if (FAILED(dred->GetAutoBreadcrumbsOutput(&output))) {
			fyuu_rhi::log::Error("D3D12 DRED failed to query automatic breadcrumbs");
			return;
		}
		for (auto node = output.pHeadAutoBreadcrumbNode; node; node = node->pNext) {
			auto completed = node->pLastBreadcrumbValue ?
				*node->pLastBreadcrumbValue :
				0u;
			fyuu_rhi::log::Fatal(
				std::format(
					"D3D12 DRED breadcrumb: queue='{}', list='{}', completed={}/{}",
					node->pCommandQueueDebugNameA ?
						node->pCommandQueueDebugNameA :
						"[unnamed]",
					node->pCommandListDebugNameA ?
						node->pCommandListDebugNameA :
						"[unnamed]",
					completed,
					node->BreadcrumbCount
				)
			);
			if (
				node->pCommandHistory &&
				completed < node->BreadcrumbCount
			) {
				fyuu_rhi::log::Fatal(
					std::format(
						"D3D12 DRED failing operation: {}",
						static_cast<std::uint32_t>(
							node->pCommandHistory[completed]
						)
					)
				);
			}
		}
	}

	void LogAllocations(std::string_view label, D3D12_DRED_ALLOCATION_NODE const* allocation) noexcept {
		for (auto node = allocation; node; node = node->pNext) {
			fyuu_rhi::log::Fatal(
				std::format(
					"D3D12 DRED {} allocation: name='{}', type={}",
					label,
					node->ObjectNameA ? node->ObjectNameA : "[unnamed]",
					static_cast<std::uint32_t>(node->AllocationType)
				)
			);
		}
	}

	void LogPageFault(ID3D12DeviceRemovedExtendedData* dred) noexcept {
		D3D12_DRED_PAGE_FAULT_OUTPUT output{};
		if (FAILED(dred->GetPageFaultAllocationOutput(&output))) {
			fyuu_rhi::log::Error("D3D12 DRED failed to query page-fault data");
			return;
		}
		fyuu_rhi::log::Fatal(
			std::format(
				"D3D12 DRED page fault at GPU virtual address 0x{:X}",
				output.PageFaultVA
			)
		);
		LogAllocations("existing", output.pHeadExistingAllocationNode);
		LogAllocations("recently freed", output.pHeadRecentFreedAllocationNode);
	}

	void CALLBACK DeviceRemoved(void* context, BOOLEAN) noexcept {
		auto device = static_cast<ID3D12Device*>(context);
		auto reason = device->GetDeviceRemovedReason();
		if (reason == S_OK) {
			return;
		}
		fyuu_rhi::log::Fatal(
			std::format(
				"D3D12 device removed with HRESULT 0x{:08X}",
				static_cast<std::uint32_t>(reason)
			)
		);
		Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
		if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dred)))) {
			fyuu_rhi::log::Error("D3D12 DRED is unavailable for the removed device");
			return;
		}
		LogBreadcrumbs(dred.Get());
		LogPageFault(dred.Get());
	}

	Microsoft::WRL::ComPtr<ID3D12Fence> CreateRemovalFence(Microsoft::WRL::ComPtr<ID3D12Device> const& device, wil::unique_event const& event) {
		Microsoft::WRL::ComPtr<ID3D12Fence> result;
		fyuu_rhi::d3d12::ThrowIfFailed(
			device->CreateFence(
				0u,
				D3D12_FENCE_FLAG_NONE,
				IID_PPV_ARGS(&result)
			)
		);
		fyuu_rhi::d3d12::ThrowIfFailed(
			result->SetEventOnCompletion(
				(std::numeric_limits<std::uint64_t>::max)(),
				event.get()
			)
		);
		return result;
	}

	HANDLE RegisterRemovalWait(Microsoft::WRL::ComPtr<ID3D12Device> const& device, wil::unique_event const& event) noexcept {
		HANDLE result = nullptr;
		if (!RegisterWaitForSingleObject(&result, event.get(), DeviceRemoved, device.Get(), INFINITE, WT_EXECUTEONLYONCE)) {
			fyuu_rhi::log::Warning("D3D12 failed to register device-removal reporting");
		}
		return result;
	}

	Microsoft::WRL::ComPtr<ID3D12InfoQueue1> CreateInfoQueue(Microsoft::WRL::ComPtr<ID3D12Device> const& device) noexcept {
		Microsoft::WRL::ComPtr<ID3D12InfoQueue1> result;
#if !defined(NDEBUG)
		if (FAILED(device.As(&result))) {
			fyuu_rhi::log::Warning("D3D12 debug-message logging is unavailable");
		}
#endif // !defined(NDEBUG)
		return result;
	}

	DWORD RegisterMessageCallback(Microsoft::WRL::ComPtr<ID3D12InfoQueue1> const& info_queue) noexcept {
		if (!info_queue) {
			return 0u;
		}
		(void)info_queue->SetBreakOnSeverity(
			D3D12_MESSAGE_SEVERITY_CORRUPTION,
			TRUE
		);
		(void)info_queue->SetBreakOnSeverity(
			D3D12_MESSAGE_SEVERITY_ERROR,
			TRUE
		);
		DWORD result = 0u;
		if (FAILED(
			info_queue->RegisterMessageCallback(
				LogD3D12Message,
				D3D12_MESSAGE_CALLBACK_FLAG_NONE,
				nullptr,
				&result
			)
		)) {
			fyuu_rhi::log::Warning("D3D12 failed to register its debug-message callback");
			return 0u;
		}
		return result;
	}

} // namespace

namespace fyuu_rhi::d3d12 {

	class DeviceRemovalTracker {
	private:
		wil::unique_event m_event;
		Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
		HANDLE m_wait;
		Microsoft::WRL::ComPtr<ID3D12InfoQueue1> m_info_queue;
		DWORD m_callback_cookie;

	public:
		explicit DeviceRemovalTracker(Microsoft::WRL::ComPtr<ID3D12Device> const& device)
			: m_event(wil::EventOptions::None),
			m_fence(CreateRemovalFence(device, m_event)),
			m_wait(RegisterRemovalWait(device, m_event)),
			m_info_queue(CreateInfoQueue(device)),
			m_callback_cookie(RegisterMessageCallback(m_info_queue)) {
		}

		DeviceRemovalTracker(DeviceRemovalTracker const&) = delete;
		DeviceRemovalTracker& operator=(DeviceRemovalTracker const&) = delete;

		DeviceRemovalTracker(DeviceRemovalTracker&& other) noexcept
			: m_event(std::move(other.m_event)),
			m_fence(std::move(other.m_fence)),
			m_wait(std::exchange(other.m_wait, nullptr)),
			m_info_queue(std::move(other.m_info_queue)),
			m_callback_cookie(std::exchange(other.m_callback_cookie, 0u)) {

		}

		DeviceRemovalTracker& operator=(DeviceRemovalTracker&& other) noexcept {
			if (this != &other) {
				m_event = std::move(other.m_event);
				m_fence = std::move(other.m_fence);
				m_wait = std::exchange(other.m_wait, nullptr);
				m_info_queue = std::move(other.m_info_queue);
				m_callback_cookie = std::exchange(other.m_callback_cookie, 0u);
			}
			return *this;
		}

		~DeviceRemovalTracker() noexcept {
			if (m_info_queue && m_callback_cookie != 0u) {
				(void)m_info_queue->UnregisterMessageCallback(m_callback_cookie);
			}
			if (m_wait) {
				(void)UnregisterWaitEx(m_wait, INVALID_HANDLE_VALUE);
			}
		}
	};

} // namespace fyuu_rhi::d3d12
#endif // defined(_WIN32)
