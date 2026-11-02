module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <algorithm>
#include <iterator>

#include <string>

#include <cstdint>

#include <optional>
#include <string_view>

#include <ranges>
#include <span>

#include <format>
#endif // !defined(__cpp_lib_modules)
#if defined(__EMSCRIPTEN__)
#include <webgpu/webgpu_cpp.h>
#else
#include <dawn/webgpu_cpp.h>
#endif // defined(__EMSCRIPTEN__)

module fyuu_rhi:webgpu_physical_device;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :log;
import :logical_device_factory;
import :physical_device_dispatch;
import :memory_pressure;
import :physical_device_factory;
import :webgpu_data;

namespace {

	fyuu_rhi::PhysicalDevice::Info::Type GetPhysicalDeviceType(wgpu::AdapterType type) noexcept {
		using Type = fyuu_rhi::PhysicalDevice::Info::Type;
		switch (type) {
		case wgpu::AdapterType::DiscreteGPU:
			return Type::DiscreteGPU;
		case wgpu::AdapterType::IntegratedGPU:
			return Type::IntegratedGPU;
		case wgpu::AdapterType::CPU:
			return Type::CPU;
		default:
			return Type::Unknown;
		}
	}

	char const* DeviceLostReasonName(wgpu::DeviceLostReason reason) noexcept {
		switch (reason) {
		case wgpu::DeviceLostReason::Unknown: return "Unknown";
		case wgpu::DeviceLostReason::Destroyed: return "Destroyed";
		case wgpu::DeviceLostReason::CallbackCancelled: return "CallbackCancelled";
		case wgpu::DeviceLostReason::FailedCreation: return "FailedCreation";
		default: return "Unexpected";
		}
	}

	/// The text both the log line and the device's failure latch carry for a lost device.
	std::string DeviceLostText(wgpu::DeviceLostReason reason, wgpu::StringView message) {
		return std::format(
			"[WebGPU] Device lost! Reason: {}\nAdditional message: {}",
			DeviceLostReasonName(reason),
			std::string_view(message.data, message.length)
		);
	}

	void OnDeviceLost(
		wgpu::Device const&,
		wgpu::DeviceLostReason reason,
		wgpu::StringView message
	) noexcept {
		try {
			fyuu_rhi::log::Fatal(DeviceLostText(reason, message));
		}
		catch (...) {
			fyuu_rhi::log::Fatal("[WebGPU] Device lost");
		}
	}

	char const* ErrorTypeName(wgpu::ErrorType type) noexcept {
		switch (type) {
		case wgpu::ErrorType::NoError: return "NoError";
		case wgpu::ErrorType::Validation: return "Validation";
		case wgpu::ErrorType::OutOfMemory: return "OutOfMemory";
		case wgpu::ErrorType::Internal: return "Internal";
		default: return "Unknown";
		}
	}

	/// The text both the log line and the device's failure latch carry for an uncaptured error.
	std::string UncapturedErrorText(wgpu::ErrorType type, wgpu::StringView message) {
		return std::format(
			"[WebGPU] Uncaptured error ({}): {}",
			ErrorTypeName(type),
			std::string_view(message.data, message.length)
		);
	}

	/// Whether an uncaptured error means the work the device was given is no longer trustworthy.
	///
	/// A rejected command buffer is reported this way and nothing else observes it: Dawn does not
	/// fail the queue-work future and a mapping of a resource that work would have produced never
	/// completes. Latching it is what turns both into a reported error.
	bool IsDeviceFailure(wgpu::ErrorType type) noexcept {
		return type == wgpu::ErrorType::Validation ||
			type == wgpu::ErrorType::Internal ||
			type == wgpu::ErrorType::OutOfMemory;
	}

	void OnUncapturedError(wgpu::ErrorType type, wgpu::StringView message) noexcept {
		try {
			auto text = UncapturedErrorText(type, message);
			if (type == wgpu::ErrorType::Validation || type == wgpu::ErrorType::Internal) {
				fyuu_rhi::log::Error(text);
			}
			else {
				fyuu_rhi::log::Warning(text);
			}
		}
		catch (...) {
			fyuu_rhi::log::Error("[WebGPU] Uncaptured error");
		}
	}

	/// Dawn's uncaptured-error callback carrying the device's failure latch as user data.
	///
	/// The C++ wrapper refuses a capturing lambda for this callback ("Uncaptured error callback
	/// cannot be a binding lambda"), so the latch travels through the API's own user data
	/// parameter instead of through the closure.
	void OnUncapturedErrorWithLatch(
		wgpu::Device const&,
		wgpu::ErrorType type,
		wgpu::StringView message,
		fyuu_rhi::webgpu::DeviceError* errors
	) noexcept {
		OnUncapturedError(type, message);
		if (!IsDeviceFailure(type)) {
			return;
		}
		try {
			errors->Fail(UncapturedErrorText(type, message));
		}
		catch (...) {
		}
	}

	bool HasFeature(wgpu::SupportedFeatures const& supported, wgpu::FeatureName name) noexcept {
		auto features = std::span(supported.features, supported.featureCount);
		return std::ranges::find(features, name) != features.end();
	}

} // namespace

namespace fyuu_rhi {

	template <>
	struct GetPhysicalDeviceInfo<webgpu::PhysicalDevice> {
		webgpu::PhysicalDevice const* native;

		PhysicalDevice::Info operator()() const {
			wgpu::AdapterInfo adapter_info;
			if (!native->adapter.GetInfo(&adapter_info)) {
				throw std::runtime_error("Failed to query WebGPU adapter information");
			}
			return {
				.name = std::string(
					adapter_info.description.data,
					adapter_info.description.length
				),
				.vendor_id = adapter_info.vendorID,
				.device_id = adapter_info.deviceID,
				.dedicated_memory = std::nullopt,
				.type = GetPhysicalDeviceType(adapter_info.adapterType)
			};
		}
	};

	template <>
	struct CreateLogicalDevice<webgpu::PhysicalDevice> {
		webgpu::PhysicalDevice const* physical_device;

		LogicalDevice operator()() const {
			// One latch per device, shared with every resource it creates and with its
			// command scheduler, so a failure reported here reaches both the submit that
			// produced it and a later host mapping.
			auto errors = std::make_shared<webgpu::DeviceError>();
			wgpu::DeviceDescriptor descriptor;
			descriptor.label = "fyuu-rhi";
#if defined(__EMSCRIPTEN__)
			descriptor.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous, OnDeviceLost);
#else
			descriptor.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents, OnDeviceLost);
#endif // defined(__EMSCRIPTEN__)
			descriptor.SetUncapturedErrorCallback(OnUncapturedErrorWithLatch, errors.get());

			// A device request may only name features the adapter advertises;
			// naming an unsupported feature is a hard creation failure, so each
			// one is gated and skipped with a warning when unavailable.
			wgpu::SupportedFeatures supported_features;
			physical_device->adapter.GetFeatures(&supported_features);
			struct FeatureRequest {
				wgpu::FeatureName name;
				std::string_view label;
			};
			static constexpr FeatureRequest requests[] = {
#if !defined(__EMSCRIPTEN__)
				{ wgpu::FeatureName::ImplicitDeviceSynchronization, "ImplicitDeviceSynchronization" },
#endif // !defined(__EMSCRIPTEN__)
				{ wgpu::FeatureName::TextureCompressionBC, "TextureCompressionBC" },
				{ wgpu::FeatureName::Unorm16TextureFormats, "Unorm16TextureFormats" },
				{ wgpu::FeatureName::TextureFormatsTier1, "TextureFormatsTier1" },
#if !defined(__EMSCRIPTEN__)
				{ wgpu::FeatureName::Unorm16Filterable, "Unorm16Filterable" },
#endif // !defined(__EMSCRIPTEN__)
				{ wgpu::FeatureName::TextureFormatsTier2, "TextureFormatsTier2" },
				{ wgpu::FeatureName::Depth32FloatStencil8, "Depth32FloatStencil8" },
				{ wgpu::FeatureName::TimestampQuery, "TimestampQuery" },
				{ wgpu::FeatureName::IndirectFirstInstance, "IndirectFirstInstance" },
				{ wgpu::FeatureName::ShaderF16, "ShaderF16" },
				{ wgpu::FeatureName::DualSourceBlending, "DualSourceBlending" },
				{ wgpu::FeatureName::Float32Filterable, "Float32Filterable" },
				{ wgpu::FeatureName::Subgroups, "Subgroups" }
			};
			std::vector<wgpu::FeatureName> required_features;
			required_features.reserve(std::size(requests));
			for (auto const& request : requests) {
				if (HasFeature(supported_features, request.name)) {
					required_features.push_back(request.name);
#if !defined(__EMSCRIPTEN__)
					if (request.name == wgpu::FeatureName::ImplicitDeviceSynchronization) {
						errors->implicit_synchronization = true;
					}
#endif // !defined(__EMSCRIPTEN__)
					continue;
				}
				try {
					log::Warning(
						std::format(
							"[WebGPU] Device feature {} is not supported by the adapter; skipping",
							request.label
						)
					);
				}
				catch (...) {
					log::Warning("[WebGPU] A device feature is not supported by the adapter; skipping");
				}
			}
			descriptor.requiredFeatureCount = required_features.size();
			if (!errors->implicit_synchronization) {
				log::Warning("[WebGPU] ImplicitDeviceSynchronization unavailable; using external device API synchronization");
			}
			descriptor.requiredFeatures = required_features.data();

			// The WebGPU default limits are deliberately conservative for the
			// web; a native engine should request the maximums the adapter
			// supports.
			wgpu::Limits limits;
			if (physical_device->adapter.GetLimits(&limits)) {
				descriptor.requiredLimits = &limits;
			}

#if defined(__EMSCRIPTEN__)
			/// @brief Asyncify suspends the synchronous RHI call while the browser requests the device.
			wgpu::Device device;
			std::string request_error;
			auto future = physical_device->adapter.RequestDevice(
				&descriptor,
				wgpu::CallbackMode::WaitAnyOnly,
				[&device, &request_error](wgpu::RequestDeviceStatus status, wgpu::Device result, wgpu::StringView message) {
					if (status == wgpu::RequestDeviceStatus::Success) {
						device = std::move(result);
					} else if (message.data && message.length != 0u) {
						request_error.assign(message.data, message.length);
					}
				}
			);
			if (physical_device->instance.WaitAny(future, UINT64_MAX) != wgpu::WaitStatus::Success || !device) {
				throw std::runtime_error(
					request_error.empty() ? "Browser WebGPU device creation failed" : request_error
				);
			}
#else
			wgpu::Device device = physical_device->adapter.CreateDevice(&descriptor);
#endif // defined(__EMSCRIPTEN__)
			if (!device) {
				throw std::runtime_error("WebGPU device creation failed; verify that the selected backend runtime is installed");
			}
			return MakeLogicalDevice(
				webgpu::LogicalDevice{
					physical_device->instance,
					physical_device->adapter,
					std::move(errors),
					std::move(device)
				}
			);
		}
	};

} // namespace fyuu_rhi
