module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <format>
#include <source_location>
#endif // !defined(__cpp_lib_modules)
#if defined(_WIN32)
#include <Windows.h>
#elif defined(__linux__)
#include <X11/Xlib.h>
#include <wayland-client-core.h>
#include <wayland-util.h>
#elif defined(__ANDROID__)
#include <android/native_window.h>
#include <android/android_native_app_glue.h>
#endif // defined(_WIN32)
#include <webgpu/webgpu_cpp.h>
module fyuu_rhi:webgpu_instance;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :webgpu_traits;
import :log;
import :cache_system;

namespace fyuu_rhi::webgpu {

	wgpu::Instance Backend::CreateInstance(
		std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver
#if defined(__ANDROID__)
		, android_app* android_app
#endif // defined(__ANDROID__)
	) {
		cache::Initialize(
			app_name, app_ver, engine_name, engine_ver
#if defined(__ANDROID__)
			, android_app
#endif // defined(__ANDROID__)
		);
		if (!wgpu::HasInstanceFeature(wgpu::InstanceFeatureName::TimedWaitAny)) {
			throw std::runtime_error("The WebGPU implementation does not support timed WaitAny");
		}
		constexpr wgpu::InstanceFeatureName RequiredFeatures[]{
			wgpu::InstanceFeatureName::TimedWaitAny
		};
		wgpu::InstanceDescriptor desc{
			.requiredFeatureCount = std::size(RequiredFeatures),
			.requiredFeatures = RequiredFeatures
		};
		return wgpu::CreateInstance(&desc);
	}

	wgpu::Adapter Backend::EnumeratePhysicalDevices(wgpu::Instance const& instance) {
		wgpu::RequestAdapterOptions options{
			.featureLevel = wgpu::FeatureLevel::Compatibility,
			.powerPreference = wgpu::PowerPreference::HighPerformance,
#if defined(_WIN32)
			.backendType = wgpu::BackendType::D3D12
#else
			.backendType = wgpu::BackendType::Undefined
#endif // defined(_WIN32)
		};

		wgpu::Adapter adapter;
		auto SetAdapter = [&adapter](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter_, char const* message) {
			if (status != wgpu::RequestAdapterStatus::Success) {
				log::Fatal(std::format("Calling RequestAdapter(), WebGPU reported: {}", message), std::source_location::current());
				return;
			}
			adapter = std::move(adapter_);
		};

		auto future = instance.RequestAdapter(
			&options,
			wgpu::CallbackMode::AllowProcessEvents,
			SetAdapter
		);

		wgpu::WaitStatus status = instance.WaitAny(future, (std::numeric_limits<std::uint64_t>::max)());

		if (status != wgpu::WaitStatus::Success) {
			throw std::runtime_error(std::format(
				"Calling WaitAny(), but instance waiting failed with status {}",
				static_cast<std::uint32_t>(status)
			));
		}
		if (!adapter) {
			throw std::runtime_error("RequestAdapter() completed without returning an adapter");
		}

		return adapter;

	}

}
