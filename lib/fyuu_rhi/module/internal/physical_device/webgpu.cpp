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
#include <dawn/webgpu_cpp.h>

module fyuu_rhi:webgpu_physical_device;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :log;
import :logical_device_factory;
import :physical_device_dispatch;
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

	void OnDeviceLost(wgpu::Device const&, wgpu::DeviceLostReason reason, wgpu::StringView message) noexcept {
		std::string_view message_view(message.data, message.length);
		try {
			fyuu_rhi::log::Fatal(std::format(
				"[WebGPU] Device lost! Reason: {}\nAdditional message: {}",
				DeviceLostReasonName(reason),
				message_view
			));
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

	void OnUncapturedError(wgpu::Device const&,	wgpu::ErrorType type, wgpu::StringView message) noexcept {
		std::string_view message_view(message.data, message.length);
		try {
			auto text = std::format(
				"[WebGPU] Uncaptured error ({}): {}",
				ErrorTypeName(type),
				message_view
			);
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
			wgpu::DeviceDescriptor descriptor;
			descriptor.label = "fyuu-rhi";
			descriptor.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents, OnDeviceLost);
			descriptor.SetUncapturedErrorCallback(OnUncapturedError);

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
				{ wgpu::FeatureName::TextureCompressionBC, "TextureCompressionBC" },
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
			descriptor.requiredFeatures = required_features.data();

			// The WebGPU default limits are deliberately conservative for the
			// web; a native engine should request the maximums the adapter
			// supports.
			wgpu::Limits limits;
			if (physical_device->adapter.GetLimits(&limits)) {
				descriptor.requiredLimits = &limits;
			}

			wgpu::Device device = physical_device->adapter.CreateDevice(&descriptor);
			if (!device) {
				throw std::runtime_error("WebGPU device creation failed; verify that the selected backend runtime is installed");
			}
			return MakeLogicalDevice(
				webgpu::LogicalDevice{
					physical_device->instance,
					physical_device->adapter,
					std::move(device)
				}
			);
		}
	};

} // namespace fyuu_rhi
