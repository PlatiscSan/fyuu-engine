module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>
#include <mutex>

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

namespace {
	bool s_is_init;
	bool s_is_wayland;
	std::string s_app_name;
	fyuu_rhi::Version s_app_ver{};
	std::string s_engine_name;
	fyuu_rhi::Version s_engine_ver{};
}

namespace fyuu_rhi {

	bool IsInitialized() noexcept {
		return s_is_init;
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
		static std::once_flag once;
		std::call_once(
			once,
			[=]() {
				s_app_name = app_name;
				s_app_ver = app_ver;
				s_engine_name = engine_name;
				s_engine_ver = engine_ver;
				cache::Initialize(
					app_name, app_ver,
					engine_name, engine_ver
#if defined(__ANDROID__)
					, android_app
#endif // defined(__ANDROID__)					
				);
				log::SetSink(sink);
				s_is_wayland = std::getenv("WAYLAND_DISPLAY");
				s_is_init = true;
			}
		);
	}

}
