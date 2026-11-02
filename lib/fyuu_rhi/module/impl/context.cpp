module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstdlib>
#include <stdexcept>
#include <utility>

#include <string>

#include <atomic>

#include <string_view>
#endif // !defined(__cpp_lib_modules)
#if defined(__ANDROID__)
#include <android_native_app_glue.h>
#endif // defined(__ANDROID__)
module fyuu_rhi;
#if	defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core;
import :cache;
import :log;
import :memory_pressure;
import :slang;

namespace {
	std::atomic_bool s_is_init = false;
	bool s_is_wayland;
	std::string s_app_name;
	fyuu_rhi::Version s_app_ver{};
	std::string s_engine_name;
	fyuu_rhi::Version s_engine_ver{};
}

namespace fyuu_rhi {
	/** @brief Invalidates the Vulkan root after the host has released its derived objects. */
	void RefreshVulkanInstance();

	void RebindRHISink(log::Sink* sink) {
		if (!s_is_init.load(std::memory_order_acquire)) {
			throw std::logic_error("InitializeRHIContext() must complete before RebindRHISink()");
		}
		log::SetSink(sink);
	}

	bool IsInitialized() noexcept {
		return s_is_init.load(std::memory_order_acquire);
	}

	std::string_view ApplicationName() noexcept {
		return s_app_name;
	}

	Version ApplicationVersion() noexcept {
		return s_app_ver;
	}

	std::string_view EngineName() noexcept {
		return s_engine_name;
	}

	Version EngineVersion() noexcept {
		return s_engine_ver;
	}

	bool IsWayland() noexcept {
		return s_is_wayland;
	}

	void InitializeRHIContext(		
		std::string_view app_name, Version const& app_ver, 
		std::string_view engine_name, Version const& engine_ver,
#if defined(__ANDROID__)
		android_app* android_app,
#endif // defined(__ANDROID__)
		log::Sink* sink
	) {
		/// @brief Own incoming views before replacing strings that may back those views.
		std::string next_app_name(app_name);
		std::string next_engine_name(engine_name);
		cache::Initialize(
			next_app_name, app_ver,
			next_engine_name, engine_ver
#if defined(__ANDROID__)
			, android_app
#endif // defined(__ANDROID__)					
		);
		s_app_name = std::move(next_app_name);
		s_app_ver = app_ver;
		s_engine_name = std::move(next_engine_name);
		s_engine_ver = engine_ver;
		s_is_wayland = std::getenv("WAYLAND_DISPLAY") != nullptr;
		/// @brief Each initialization renews the borrowed Sink, including after a host restart.
		log::SetSink(sink);
		shader::RefreshSlang();
		RefreshVulkanInstance();
		s_is_init.store(true, std::memory_order_release);
		details::StartProcessMemorySummaries();
	}

}
