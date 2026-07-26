module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <format>
#include <source_location>
#endif // !defined(__cpp_lib_modules)
#include <webgpu/webgpu_cpp.h>
#include "log.hpp"
module fyuu_rhi:webgpu_physical_device;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :webgpu_traits;
import :core_types;
import :log;

namespace {

	namespace log = fyuu_rhi::log;

	fyuu_rhi::PhysicalDeviceInfo::Type GetPhysicalDeviceType(wgpu::AdapterInfo const& adapter_info) {
		using Type = fyuu_rhi::PhysicalDeviceInfo::Type;
		switch (adapter_info.adapterType) {
		case wgpu::AdapterType::DiscreteGPU: return Type::DiscreteGPU;
		case wgpu::AdapterType::IntegratedGPU: return Type::IntegratedGPU;
		case wgpu::AdapterType::CPU: return Type::CPU;
		default: return Type::Unknown;
		}
	}

	void OnDeviceLost(wgpu::Device const&, wgpu::DeviceLostReason reason, wgpu::StringView message) {
		std::string_view msg_view(message.data, message.length);
		char const* reason_str;
		switch (reason) {
		case wgpu::DeviceLostReason::Unknown: reason_str = "Unknown"; break;
		case wgpu::DeviceLostReason::Destroyed: reason_str = "Destroyed"; break;
		case wgpu::DeviceLostReason::CallbackCancelled: reason_str = "CallbackCancelled"; break;
		case wgpu::DeviceLostReason::FailedCreation: reason_str = "FailedCreation"; break;
		default: reason_str = "Unexpected";
		}
		LOG_FATAL(std::format("[WebGPU] Device lost! Reason: {}\nAdditional message: {}", reason_str, msg_view));
	}

	void OnUncapturedError(wgpu::Device const& device, wgpu::ErrorType type, wgpu::StringView message) {
		std::string_view msg_view(message.data, message.length);
		char const* type_str;
		switch (type) {
		case wgpu::ErrorType::NoError: type_str = "NoError"; break;
		case wgpu::ErrorType::Validation: type_str = "Validation"; break;
		case wgpu::ErrorType::OutOfMemory: type_str = "OutOfMemory"; break;
		case wgpu::ErrorType::Internal: type_str = "Internal"; break;
		default: type_str = "Unknown"; break;
		}
		std::string log_msg = std::format(
			"[WebGPU] Uncaptured error ({}): {}\nDevice: {}",
			type_str,
			msg_view,
			reinterpret_cast<std::uintptr_t>(&device)
		);
		if (type == wgpu::ErrorType::Validation || type == wgpu::ErrorType::Internal) {
			LOG_ERROR(log_msg);
		}
		else {
			LOG_WARNING(log_msg);
		}
	}

}

namespace fyuu_rhi::webgpu {

	PhysicalDeviceInfo Backend::GetPhysicalDeviceInfo(wgpu::Adapter const& phys_dev) {

		wgpu::AdapterInfo adapter_info;
		wgpu::ConvertibleStatus status = phys_dev.GetInfo(&adapter_info);

		if (!status) {
			throw std::runtime_error("Failed to call GetInfo()");
		}

		PhysicalDeviceInfo info = {
			.name = std::string(adapter_info.description.data, adapter_info.description.length),
			.vendor_id = adapter_info.vendorID,
			.device_id = adapter_info.deviceID,
			.dedicated_memory = std::nullopt,
			.type = GetPhysicalDeviceType(adapter_info)
		};

		return info;

	}

	Backend::LogicalDevice Backend::CreateLogicalDevice(wgpu::Adapter const& adapter) {
		wgpu::DeviceDescriptor desc;
		desc.SetDeviceLostCallback(
			wgpu::CallbackMode::AllowProcessEvents,
			OnDeviceLost
		);
		desc.SetUncapturedErrorCallback(
			OnUncapturedError
		);
		auto device = adapter.CreateDevice(&desc);
		if (!device) {
			throw std::runtime_error(
				"CreateDevice() failed; verify that the selected WebGPU backend runtime is installed"
			);
		}

		return {
			device,
			adapter,
			std::make_shared<LogicalDevice::PresentationCache>(),
			execution::CompletionService::Instance()
		};

	}

}
