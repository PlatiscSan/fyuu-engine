module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <limits>
#include <utility>
#include <string>
#include <format>
#include <optional>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <strsafe.h>
#include <dxgi1_3.h>
#include <d3d12.h>
#include <wrl.h>
#include <comdef.h>
#include <wil/resource.h>
#endif // defined(_WIN32)
#define BOOST_DISABLE_ASSERTS
#include <boost/locale.hpp>
#include "log.hpp"
module fyuu_rhi:d3d12_device_removal_tracker;
#if defined(_WIN32)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :d3d12_utility;
import :log;

namespace {

	using namespace fyuu_rhi;

	using UniqueWait = wil::unique_any<HANDLE, decltype(&::UnregisterWait), ::UnregisterWait>;

	void D3D12MessageCallback(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID ID, LPCSTR description, void* context) {
	
		std::string formatted_message = std::format("[D3D12] Category: {}, ID: {} - {}", static_cast<std::size_t>(category), static_cast<std::size_t>(ID), description);
		
		// Map severity
		switch (severity) {
		case D3D12_MESSAGE_SEVERITY_CORRUPTION:
			LOG_FATAL(formatted_message);
			break;
		case D3D12_MESSAGE_SEVERITY_ERROR:
			LOG_ERROR(formatted_message);
			break;
		case D3D12_MESSAGE_SEVERITY_WARNING:
			LOG_WARNING(formatted_message);
			break;
		case D3D12_MESSAGE_SEVERITY_INFO:
			LOG_INFO(formatted_message);
			break;
		case D3D12_MESSAGE_SEVERITY_MESSAGE:
		default:
			LOG_TRACE(formatted_message);
			break;
		}

	}

	constexpr std::string_view D3D12_DRED_ALLOCATION_TYPE_to_string(D3D12_DRED_ALLOCATION_TYPE type) noexcept {
		switch (type) {
		case D3D12_DRED_ALLOCATION_TYPE_COMMAND_QUEUE: return "COMMAND_QUEUE";
		case D3D12_DRED_ALLOCATION_TYPE_COMMAND_ALLOCATOR:return "COMMAND_ALLOCATOR";
		case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_STATE: return "PIPELINE_STATE";
		case D3D12_DRED_ALLOCATION_TYPE_COMMAND_LIST: return "COMMAND_LIST";
		case D3D12_DRED_ALLOCATION_TYPE_FENCE: return "FENCE";
		case D3D12_DRED_ALLOCATION_TYPE_DESCRIPTOR_HEAP:return "DESCRIPTOR_HEAP";
		case D3D12_DRED_ALLOCATION_TYPE_HEAP:return "HEAP";
		case D3D12_DRED_ALLOCATION_TYPE_QUERY_HEAP:return "QUERY_HEAP";
		case D3D12_DRED_ALLOCATION_TYPE_COMMAND_SIGNATURE:return "COMMAND_SIGNATURE";
		case D3D12_DRED_ALLOCATION_TYPE_PIPELINE_LIBRARY: return "PIPELINE_LIBRARY";
		case D3D12_DRED_ALLOCATION_TYPE_VIDEO_DECODER:return "VIDEO_DECODER";
		case D3D12_DRED_ALLOCATION_TYPE_VIDEO_PROCESSOR:return "VIDEO_PROCESSOR";
		case D3D12_DRED_ALLOCATION_TYPE_RESOURCE:return "RESOURCE";
		case D3D12_DRED_ALLOCATION_TYPE_PASS:return "PASS";
		case D3D12_DRED_ALLOCATION_TYPE_CRYPTOSESSION:return "CRYPTOSESSION";
		case D3D12_DRED_ALLOCATION_TYPE_CRYPTOSESSIONPOLICY: return "CRYPTOSESSIONPOLICY";
		case D3D12_DRED_ALLOCATION_TYPE_PROTECTEDRESOURCESESSION: return "PROTECTEDRESOURCESESSION";
		case D3D12_DRED_ALLOCATION_TYPE_VIDEO_DECODER_HEAP:return "VIDEO_DECODER_HEAP";
		case D3D12_DRED_ALLOCATION_TYPE_COMMAND_POOL: return "COMMAND_POOL";
		case D3D12_DRED_ALLOCATION_TYPE_COMMAND_RECORDER: return "COMMAND_RECORDER";
		case D3D12_DRED_ALLOCATION_TYPE_STATE_OBJECT: return "STATE_OBJECT";
		case D3D12_DRED_ALLOCATION_TYPE_METACOMMAND:return "METACOMMAND";
		case D3D12_DRED_ALLOCATION_TYPE_SCHEDULINGGROUP:return "SCHEDULINGGROUP";
		case D3D12_DRED_ALLOCATION_TYPE_VIDEO_MOTION_ESTIMATOR:return "VIDEO_MOTION_ESTIMATOR";
		case D3D12_DRED_ALLOCATION_TYPE_VIDEO_MOTION_VECTOR_HEAP: return "VIDEO_MOTION_VECTOR_HEAP";
		case D3D12_DRED_ALLOCATION_TYPE_VIDEO_EXTENSION_COMMAND: return "VIDEO_EXTENSION_COMMAND";
		case D3D12_DRED_ALLOCATION_TYPE_VIDEO_ENCODER:return "VIDEO_ENCODER";
		case D3D12_DRED_ALLOCATION_TYPE_VIDEO_ENCODER_HEAP:return "VIDEO_ENCODER_HEAP";
		case D3D12_DRED_ALLOCATION_TYPE_INVALID: return "INVALID";
		default: return "Unknown D3D12_DRED_ALLOCATION_TYPE";
		}
	}

	constexpr std::string_view D3D12AutoBreadcrumbOpToString(D3D12_AUTO_BREADCRUMB_OP op) noexcept {
		switch (op) {
		case D3D12_AUTO_BREADCRUMB_OP_SETMARKER: return "SETMARKER";
		case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT: return "BEGINEVENT";
		case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT: return "ENDEVENT";
		case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED: return "DRAWINSTANCED";
		case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED: return "DRAWINDEXEDINSTANCED";
		case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT: return "EXECUTEINDIRECT";
		case D3D12_AUTO_BREADCRUMB_OP_DISPATCH: return "DISPATCH";
		case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION: return "COPYBUFFERREGION";
		case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION: return "COPYTEXTUREREGION";
		case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE: return "COPYRESOURCE";
		case D3D12_AUTO_BREADCRUMB_OP_COPYTILES: return "COPYTILES";
		case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE: return "RESOLVESUBRESOURCE";
		case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW:return "CLEARRENDERTARGETVIEW";
		case D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW:return "CLEARUNORDEREDACCESSVIEW";
		case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW:return "CLEARDEPTHSTENCILVIEW";
		case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER:return "RESOURCEBARRIER";
		case D3D12_AUTO_BREADCRUMB_OP_EXECUTEBUNDLE: return "EXECUTEBUNDLE";
		case D3D12_AUTO_BREADCRUMB_OP_PRESENT:return "PRESENT";
		case D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA: return "RESOLVEQUERYDATA";
		case D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION: return "BEGINSUBMISSION";
		case D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION: return "ENDSUBMISSION";
		case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME: return "DECODEFRAME";
		case D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES: return "PROCESSFRAMES";
		case D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT: return "ATOMICCOPYBUFFERUINT";
		case D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT64: return "ATOMICCOPYBUFFERUINT64";
		case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCEREGION: return "RESOLVESUBRESOURCEREGION";
		case D3D12_AUTO_BREADCRUMB_OP_WRITEBUFFERIMMEDIATE: return "WRITEBUFFERIMMEDIATE";
		case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME1: return "DECODEFRAME1";
		case D3D12_AUTO_BREADCRUMB_OP_SETPROTECTEDRESOURCESESSION: return "SETPROTECTEDRESOURCESESSION";
		case D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME2: return "DECODEFRAME2";
		case D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES1:return "PROCESSFRAMES1";
		case D3D12_AUTO_BREADCRUMB_OP_BUILDRAYTRACINGACCELERATIONSTRUCTURE: return "BUILDRAYTRACINGACCELERATIONSTRUCTURE";
		case D3D12_AUTO_BREADCRUMB_OP_EMITRAYTRACINGACCELERATIONSTRUCTUREPOSTBUILDINFO: return "EMITRAYTRACINGACCELERATIONSTRUCTUREPOSTBUILDINFO";
		case D3D12_AUTO_BREADCRUMB_OP_COPYRAYTRACINGACCELERATIONSTRUCTURE: return "COPYRAYTRACINGACCELERATIONSTRUCTURE";
		case D3D12_AUTO_BREADCRUMB_OP_DISPATCHRAYS:return "DISPATCHRAYS";
		case D3D12_AUTO_BREADCRUMB_OP_INITIALIZEMETACOMMAND: return "INITIALIZEMETACOMMAND";
		case D3D12_AUTO_BREADCRUMB_OP_EXECUTEMETACOMMAND:return "EXECUTEMETACOMMAND";
		case D3D12_AUTO_BREADCRUMB_OP_ESTIMATEMOTION: return "ESTIMATEMOTION";
		case D3D12_AUTO_BREADCRUMB_OP_RESOLVEMOTIONVECTORHEAP: return "RESOLVEMOTIONVECTORHEAP";
		case D3D12_AUTO_BREADCRUMB_OP_SETPIPELINESTATE1: return "SETPIPELINESTATE1";
		case D3D12_AUTO_BREADCRUMB_OP_INITIALIZEEXTENSIONCOMMAND: return "INITIALIZEEXTENSIONCOMMAND";
		case D3D12_AUTO_BREADCRUMB_OP_EXECUTEEXTENSIONCOMMAND: return "EXECUTEEXTENSIONCOMMAND";
		case D3D12_AUTO_BREADCRUMB_OP_DISPATCHMESH: return "DISPATCHMESH";
		case D3D12_AUTO_BREADCRUMB_OP_ENCODEFRAME: return "ENCODEFRAME";
		case D3D12_AUTO_BREADCRUMB_OP_RESOLVEENCODEROUTPUTMETADATA: return "RESOLVEENCODEROUTPUTMETADATA";
		default: return "Unknown D3D12_AUTO_BREADCRUMB_OP";
		}
	}

	void DeviceRemovedCallback(PVOID context, BOOLEAN) {

		auto dev = static_cast<ID3D12Device*>(context);

		HRESULT result = dev->GetDeviceRemovedReason();
		if (result == S_OK) {
			return; // Proper shutdown, no dev removal actually occurred.
		}

#if !defined(_NDEBUG)
		// In debug builds, log extensive DRED information.

		// Log the HRESULT result as a string.
		{
			_com_error error(result);
#if defined(_UNICODE)
			// Convert wide error message to UTF-8 using a PMR pool allocator.
			std::string message = boost::locale::conv::utf_to_utf<char>(error.ErrorMessage());
			LOG_FATAL(message);
#else
			LOG_FATAL(error.ErrorMessage());
#endif
		}

		// Query DRED (Device Removed Extended Data) interfaces.
		Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData> dred;
		result = dev->QueryInterface(IID_PPV_ARGS(&dred));
		if (FAILED(result)) {
			LOG_FATAL("Calling QueryInterface() failed then device removed information cannot be logged");
			return;
		}

		D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT dred_auto_breadcrumbs_output;
		result = dred->GetAutoBreadcrumbsOutput(&dred_auto_breadcrumbs_output);
		if (FAILED(result)) {
			LOG_FATAL("Calling ID3D12DeviceRemovedExtendedData::GetAutoBreadcrumbsOutput() failed then breadcrumbs cannot be logged");
		}
		else {
			auto node = dred_auto_breadcrumbs_output.pHeadAutoBreadcrumbNode;
			while (node) {
				LOG_FATAL(
					std::format("DRED Breadcrumb Data:\n\tCommand List:\t{}\n\tCommand Queue:\t{}\n",
						node->pCommandListDebugNameA == nullptr ? "[Unnamed CommandList]" : node->pCommandListDebugNameA,
						node->pCommandQueueDebugNameA == nullptr ? "[Unnamed CommandQueue]" : node->pCommandQueueDebugNameA
					)
				);

				for (UINT32 i = 0; i < node->BreadcrumbCount; i++) {
					auto& command = node->pCommandHistory[i];
					auto str = D3D12AutoBreadcrumbOpToString(command);
					char const* fail_str = "";
					if (i == *node->pLastBreadcrumbValue) {
						fail_str = "\t\t <- failed";
					}
					LOG_FATAL(std::format("\t\t{}{}\n", str, fail_str));
				}
				node = node->pNext;
			}
		}

		D3D12_DRED_PAGE_FAULT_OUTPUT dred_page_fault_output;
		result = dred->GetPageFaultAllocationOutput(&dred_page_fault_output);
		if (FAILED(result)) {
			LOG_FATAL(
				"Calling ID3D12DeviceRemovedExtendedData::GetPageFaultAllocationOutput() failed then page fault allocation cannot be logged"
			);
		}
		else {
			// Log page fault information.
			LOG_FATAL(
				std::format("DRED Page Fault Output:\n\tVirtual Address: {:X}", dred_page_fault_output.PageFaultVA)
			);

			// Log allocations that existed at the time of the fault.
			{
				auto node = dred_page_fault_output.pHeadExistingAllocationNode;
				while (node) {
					LOG_FATAL(
						std::format("\tDRED Page Fault Existing Allocation Node:\n\t\tObject Name: {}\n\t\tAllocation Type: {}\n",
							node->ObjectNameA ? node->ObjectNameA : "[Unnamed Object]",
							D3D12_DRED_ALLOCATION_TYPE_to_string(node->AllocationType)
						)
					);
					node = node->pNext;
				}
			}

			// Log recently freed allocations.
			{
				auto node = dred_page_fault_output.pHeadRecentFreedAllocationNode;
				while (node) {
					LOG_FATAL(
						std::format("\tDRED Page Fault Recent Freed Allocation Node:\n\t\tObject Name: {}\n\t\tAllocation Type: {}\n",
							node->ObjectNameA ? node->ObjectNameA : "[Unnamed Object]",
							D3D12_DRED_ALLOCATION_TYPE_to_string(node->AllocationType)
						)
					);
					node = node->pNext;
				}
			}

		}

#endif // !defined(_NDEBUG)
		// Final fatal message.
		LOG_FATAL("Device removal triggered!");
	}
	
}

namespace fyuu_rhi::d3d12 {
	class DeviceRemovalTracker {
	private:
		ManagedEvent m_fence_event;
		Microsoft::WRL::ComPtr<ID3D12Fence> m_dev_rm_fence;
		std::optional<UniqueWait> m_dev_rm_wait;
		Microsoft::WRL::ComPtr<ID3D12InfoQueue1> m_info_queue;
		DWORD m_cb_cookie;

	public:
		DeviceRemovalTracker(Microsoft::WRL::ComPtr<ID3D12Device> const& dev);
		DeviceRemovalTracker(DeviceRemovalTracker const&) = delete;
		DeviceRemovalTracker(DeviceRemovalTracker&& other) noexcept;
		~DeviceRemovalTracker() noexcept;
	};
}

namespace fyuu_rhi::d3d12 {
	DeviceRemovalTracker::DeviceRemovalTracker(Microsoft::WRL::ComPtr<ID3D12Device> const& dev)
		: m_fence_event(CreateManagedEvent()),
		m_dev_rm_fence(
			[this, &dev]() {
				Microsoft::WRL::ComPtr<ID3D12Fence> dev_rm_fence;
				HRESULT result = dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&dev_rm_fence));
				ThrowIfFailed(result);
				result = dev_rm_fence->SetEventOnCompletion((std::numeric_limits<std::uint64_t>::max)(), m_fence_event.impl.get());
				ThrowIfFailed(result);
				return dev_rm_fence;
			}()),
		m_dev_rm_wait(
			[this, &dev]() -> std::optional<UniqueWait> {
				HANDLE raw_wait_handle;
				if (!RegisterWaitForSingleObject(&raw_wait_handle, m_fence_event.impl.get(), DeviceRemovedCallback, dev.Get(), INFINITE, 0u)) {
					LOG_WARNING(
						"Calling RegisterWaitForSingleObject() failed then no crash report will be logged if device removal is triggered"
					);
					return std::nullopt;
				}
				return UniqueWait(raw_wait_handle);
			}()),
		m_info_queue(
#if defined(NDEBUG)
			nullptr
#else
			[&dev]() {
				Microsoft::WRL::ComPtr<ID3D12InfoQueue1> info_queue;
				HRESULT result = dev.As(&info_queue);
				if (FAILED(result)) {
					LOG_WARNING("Calling As() failed then log is unavailable");
				}
				return info_queue;
			}()
#endif // defined(NDEBUG)
				),
		m_cb_cookie(
#if defined(NDEBUG)
			0u
#else
			[this]() -> DWORD {

				if (!m_info_queue) {
					return 0u;
				}

				HRESULT result = m_info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
				if (FAILED(result)) {
					LOG_WARNING("Calling SetBreakOnSeverity() to set break point on fatal but failed then the break point is unavailable");
				}

				result = m_info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
				if (FAILED(result)) {
					LOG_WARNING("Calling SetBreakOnSeverity() to set break point on error but failed then the break point is unavailable");
				}

				DWORD cb_cookie = 0;
				result = m_info_queue->RegisterMessageCallback(
					D3D12MessageCallback,
					D3D12_MESSAGE_CALLBACK_FLAG_NONE,
					nullptr,
					&cb_cookie
				);
				if (FAILED(result)) {
					LOG_WARNING("Calling RegisterMessageCallback() failed then log is unavailable");
				}

				return cb_cookie;

			}()
#endif // defined(NDEBUG)
				) {
	}

	DeviceRemovalTracker::DeviceRemovalTracker(DeviceRemovalTracker&& other) noexcept
		: m_fence_event(std::move(other.m_fence_event)),
		m_dev_rm_fence(std::move(other.m_dev_rm_fence)),
		m_dev_rm_wait(std::move(other.m_dev_rm_wait)),
		m_info_queue(std::move(other.m_info_queue)),
		m_cb_cookie(std::exchange(other.m_cb_cookie, 0u)) {

	}

	DeviceRemovalTracker::~DeviceRemovalTracker() noexcept {
		if (m_info_queue && m_cb_cookie) {
			m_info_queue->UnregisterMessageCallback(m_cb_cookie);
		}
	}

}

#endif // defined(_WIN32)
