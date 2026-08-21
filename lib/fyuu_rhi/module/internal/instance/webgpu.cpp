module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <utility>
#include <string>
#include <limits>

#include <cstdint>

#include <format>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#include <VersionHelpers.h>
#endif // defined(_WIN32)
#include <dawn/webgpu_cpp.h>
#if defined(_WIN32)
#include <dawn/native/DawnNative.h>
#endif // defined(_WIN32)

module fyuu_rhi:webgpu_instance;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :instance_dispatch;
import :instance_factory;
import :log;
import :physical_device_factory;
import :webgpu_data;

namespace fyuu_rhi {
	template <>
	struct CreateInstance<webgpu::Instance> {
		webgpu::Instance operator()() const {
			if (!wgpu::HasInstanceFeature(wgpu::InstanceFeatureName::TimedWaitAny)) {
				throw std::runtime_error("The WebGPU implementation does not support timed WaitAny");
			}
			constexpr wgpu::InstanceFeatureName RequiredFeatures[] = {
				wgpu::InstanceFeatureName::TimedWaitAny
			};
#if defined(_WIN32)
			std::array<char, MAX_PATH> system_directory;
			auto const system_directory_size = GetSystemDirectoryA(
				system_directory.data(),
				static_cast<UINT>(system_directory.size())
			);
			if (
				system_directory_size == 0u ||
				system_directory_size >= system_directory.size()
			) {
				throw std::runtime_error("Failed to locate the Windows system directory");
			}
			std::string runtime_search_path(
				system_directory.data(),
				system_directory_size
			);
			runtime_search_path.push_back('\\');
			char const* runtime_search_paths[]{ runtime_search_path.c_str() };
			dawn::native::DawnInstanceDescriptor dawn_descriptor;
			dawn_descriptor.additionalRuntimeSearchPathsCount = std::size(
				runtime_search_paths
			);
			dawn_descriptor.additionalRuntimeSearchPaths = runtime_search_paths;
#endif // defined(_WIN32)
			wgpu::InstanceDescriptor desc{
#if defined(_WIN32)
				.nextInChain = &dawn_descriptor,
#endif // defined(_WIN32)
				.requiredFeatureCount = std::size(RequiredFeatures),
				.requiredFeatures = RequiredFeatures
			};
			return { wgpu::CreateInstance(&desc) };
		}
	};

	template <>
	struct EnumeratePhysicalDevices<webgpu::Instance> {
		webgpu::Instance const* instance;

		std::vector<PhysicalDevice> operator()() const {
			wgpu::RequestAdapterOptions options{
				.featureLevel = wgpu::FeatureLevel::Compatibility,
				.powerPreference = wgpu::PowerPreference::HighPerformance,
#if defined(_WIN32)
				.backendType = IsWindows10OrGreater() ?
					wgpu::BackendType::D3D12 :
					wgpu::BackendType::Undefined
#else
				.backendType = wgpu::BackendType::Undefined
#endif // defined(_WIN32)
			};
			wgpu::Adapter adapter;
			auto future = instance->impl.RequestAdapter(
				&options,
				wgpu::CallbackMode::AllowProcessEvents,
				[&adapter](wgpu::RequestAdapterStatus status, wgpu::Adapter result, char const* message) noexcept{
					if (status == wgpu::RequestAdapterStatus::Success) {
						adapter = std::move(result);
						return;
					}
					try {
						log::Error(
							std::format(
								"WebGPU adapter request failed with status {}: {}",
								static_cast<std::uint32_t>(status),
								message ? message : "No diagnostic message"
							)
						);
					}
					catch (...) {
						log::Error("WebGPU adapter request failed");
					}
				}
			);
			auto status = instance->impl.WaitAny(future, (std::numeric_limits<std::uint64_t>::max)());
			if (status != wgpu::WaitStatus::Success || !adapter) {
				throw std::runtime_error("Failed to request a WebGPU adapter");
			}
			std::vector<PhysicalDevice> physical_devices;
			physical_devices.emplace_back(
				MakePhysicalDevice(
					webgpu::PhysicalDevice{ instance->impl, std::move(adapter) }
				)
			);
			return physical_devices;
		}
	};
}
