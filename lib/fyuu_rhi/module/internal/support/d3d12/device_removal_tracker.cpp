module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <limits>
#include <cstdint>

#include <atomic>

#include <string_view>

#include <format>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <d3d12.h>
#include <wrl.h>
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
			auto completed = node->pLastBreadcrumbValue ? *node->pLastBreadcrumbValue : 0u;
			fyuu_rhi::log::Fatal(
			    std::format(
			        "D3D12 DRED breadcrumb: queue='{}', list='{}', completed={}/{}",
			        node->pCommandQueueDebugNameA ? node->pCommandQueueDebugNameA : "[unnamed]",
			        node->pCommandListDebugNameA ? node->pCommandListDebugNameA : "[unnamed]",
			        completed,
			        node->BreadcrumbCount
			    )
			);
			if (node->pCommandHistory && completed < node->BreadcrumbCount) {
				fyuu_rhi::log::Fatal(
				    std::format(
				        "D3D12 DRED failing operation: {}",
				        static_cast<std::uint32_t>(node->pCommandHistory[completed])
				    )
				);
			}
		}
	}

	void LogAllocations(
	    std::string_view label,
	    D3D12_DRED_ALLOCATION_NODE const* allocation
	) noexcept {
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
		    std::format("D3D12 DRED page fault at GPU virtual address 0x{:X}", output.PageFaultVA)
		);
		LogAllocations("existing", output.pHeadExistingAllocationNode);
		LogAllocations("recently freed", output.pHeadRecentFreedAllocationNode);
	}

	/// Names the removal reason and dumps what the DRED settings captured.
	///
	/// Called by whichever path detected the removal, in the order the D3D12
	/// documentation prescribes: detection first, diagnosis second. DRED reports
	/// nothing on its own, so nothing here participates in detecting a removal.
	void ReportDeviceRemoval(ID3D12Device* device) noexcept {
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

	Microsoft::WRL::ComPtr<ID3D12InfoQueue1> AsInfoQueue(
	    Microsoft::WRL::ComPtr<ID3D12Device> const& device,
	    bool log = true
	) noexcept {
		Microsoft::WRL::ComPtr<ID3D12InfoQueue1> result;
#if !defined(NDEBUG)
		if (FAILED(device.As(&result)) && log) {
			fyuu_rhi::log::Warning("D3D12 debug-message logging is unavailable");
		}
#endif // !defined(NDEBUG)
		return result;
	}

	DWORD RegisterMessageCallback(
	    Microsoft::WRL::ComPtr<ID3D12InfoQueue1> const& info_queue
	) noexcept {
		if (!info_queue) {
			return 0u;
		}
		(void)info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		(void)info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		DWORD result = 0u;
		if (FAILED(info_queue->RegisterMessageCallback(
		        LogD3D12Message,
		        D3D12_MESSAGE_CALLBACK_FLAG_NONE,
		        nullptr,
		        &result
		    ))) {
			fyuu_rhi::log::Warning("D3D12 failed to register its debug-message callback");
			return 0u;
		}
		return result;
	}

} // namespace

namespace fyuu_rhi::d3d12 {

	class DeviceRemovalTracker {
	private:
		Microsoft::WRL::ComPtr<ID3D12Device> m_device;
		DWORD m_callback_cookie;
		/// Set by the first report, so several in-flight submissions that all observe
		/// the same removal produce one diagnosis instead of one each.
		std::atomic_bool m_reported;

	public:
		explicit DeviceRemovalTracker(Microsoft::WRL::ComPtr<ID3D12Device> const& device) :
		    m_device(device), m_callback_cookie(RegisterMessageCallback(AsInfoQueue(m_device))),
		    m_reported(false) {
		}

		DeviceRemovalTracker(DeviceRemovalTracker const&) = delete;
		DeviceRemovalTracker& operator=(DeviceRemovalTracker const&) = delete;

		DeviceRemovalTracker(DeviceRemovalTracker&& other) noexcept :
		    m_device(std::move(other.m_device)),
		    m_callback_cookie(std::exchange(other.m_callback_cookie, 0u)),
		    m_reported(other.m_reported.exchange(false, std::memory_order_relaxed)) {
		}

		DeviceRemovalTracker& operator=(DeviceRemovalTracker&& other) noexcept {
			if (this != &other) {
				m_device = std::move(other.m_device);
				m_callback_cookie = std::exchange(other.m_callback_cookie, 0u);
				m_reported.store(
				    other.m_reported.exchange(false, std::memory_order_relaxed),
				    std::memory_order_relaxed
				);
			}
			return *this;
		}

		~DeviceRemovalTracker() noexcept {
			if (m_callback_cookie != 0u) {
				AsInfoQueue(m_device, false)->UnregisterMessageCallback(m_callback_cookie);
			}
		}

		Microsoft::WRL::ComPtr<ID3D12Device> GetDevice() const noexcept {
			return m_device;
		}

		/// Diagnoses one removal of this device, at most once for this tracker's life.
		/// Detection stays with the caller; this only reports what was detected.
		void ReportRemoval() noexcept {
			if (m_reported.exchange(true, std::memory_order_acq_rel)) {
				return;
			}
			ReportDeviceRemoval(m_device.Get());
		}
	};

} // namespace fyuu_rhi::d3d12
#endif // defined(_WIN32)
