module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <new>
#include <string_view>
#endif // !defined(__cpp_lib_modules)
#include "fyuu_log.h"
#include "fyuu_platform.h"
#include "fyuu_runtime.h"

module fyuu_engine:entry;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

extern "C" {

	struct Fyuu_Platform {
		Fyuu_PlatformDescriptor descriptor;
		bool initialized = false;
	};

	struct Fyuu_Runtime {
		Fyuu_RuntimeDescriptor descriptor;
		Fyuu_Platform* platform = nullptr;
		Fyuu_RuntimeState state = FYUU_RUNTIME_STATE_CREATED;
	};

}

namespace {

	// Descriptor validation is centralized here so C callers and module wrappers obey
	// identical size, version, and required-callback rules.
	bool ValidPlatformDescriptor(Fyuu_PlatformDescriptor const& descriptor) noexcept {
		return descriptor.struct_size >= sizeof(Fyuu_PlatformDescriptor) &&
			descriptor.ABI_version == FYUU_ABI_VERSION &&
			descriptor.pump_events;
	}

	bool ValidRuntimeDescriptor(Fyuu_RuntimeDescriptor const& descriptor) noexcept {
		return descriptor.struct_size >= sizeof(Fyuu_RuntimeDescriptor) &&
			descriptor.ABI_version == FYUU_ABI_VERSION;
	}

	void WriteRuntimeLog(
		Fyuu_Runtime& runtime,
		Fyuu_LogLevel level,
		std::string_view message
	) noexcept {
		if (!runtime.descriptor.logger) {
			return;
		}
		Fyuu_LogRecord const record{
			level,
			"Runtime",
			7,
			message.data(),
			message.size()
		};
		Fyuu_LoggerWrite(runtime.descriptor.logger, &record);
	}

	void StopRuntime(Fyuu_Runtime& runtime) noexcept {
		// StopRuntime is the single terminal transition. It guarantees that application
		// shutdown runs at most once before the state becomes Stopped.
		if (runtime.state == FYUU_RUNTIME_STATE_STOPPED) {
			return;
		}
		if (runtime.state == FYUU_RUNTIME_STATE_CREATED) {
			runtime.state = FYUU_RUNTIME_STATE_STOPPED;
			return;
		}
		if (runtime.descriptor.shutdown) {
			runtime.descriptor.shutdown(&runtime, runtime.descriptor.user_data);
		}
		runtime.state = FYUU_RUNTIME_STATE_STOPPED;
		WriteRuntimeLog(runtime, FYUU_LOG_LEVEL_INFO, "Runtime stopped");
	}

}

extern "C" {

	// C ABI entry points below own the shared state machine. Native module methods call
	// these functions directly; callbacks return through generated :api_glue thunks.

	LIB_API Fyuu_Result LIB_CALL Fyuu_PlatformCreate(
		Fyuu_PlatformDescriptor const* descriptor,
		Fyuu_Platform** output
	) NOEXCEPT {
		if (!descriptor || !output || !ValidPlatformDescriptor(*descriptor)) {
			return FYUU_RESULT_INVALID_ARGUMENT;
		}
		*output = nullptr;
		auto platform = new (std::nothrow) Fyuu_Platform{ *descriptor, false };
		if (!platform) {
			return FYUU_RESULT_OUT_OF_MEMORY;
		}
		*output = platform;
		return FYUU_RESULT_SUCCESS;
	}

	LIB_API void LIB_CALL Fyuu_PlatformDestroy(Fyuu_Platform* platform) NOEXCEPT {
		if (!platform) {
			return;
		}
		if (platform->initialized && platform->descriptor.shutdown) {
			platform->descriptor.shutdown(platform, platform->descriptor.user_data);
		}
		delete platform;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_RuntimeCreate(
		Fyuu_Platform* platform,
		Fyuu_RuntimeDescriptor const* descriptor,
		Fyuu_Runtime** output
	) NOEXCEPT {
		if (!platform || !descriptor || !output || !ValidRuntimeDescriptor(*descriptor)) {
			return FYUU_RESULT_INVALID_ARGUMENT;
		}
		*output = nullptr;
		auto runtime = new (std::nothrow) Fyuu_Runtime{
			*descriptor,
			platform,
			FYUU_RUNTIME_STATE_CREATED
		};
		if (!runtime) {
			return FYUU_RESULT_OUT_OF_MEMORY;
		}
		*output = runtime;
		return FYUU_RESULT_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_RuntimeInitialize(Fyuu_Runtime* runtime) NOEXCEPT {
		if (!runtime) {
			return FYUU_RESULT_INVALID_ARGUMENT;
		}
		if (runtime->state != FYUU_RUNTIME_STATE_CREATED) {
			return FYUU_RESULT_INVALID_STATE;
		}
		if (!runtime->platform->initialized && runtime->platform->descriptor.initialize) {
			auto const result = runtime->platform->descriptor.initialize(
				runtime->platform,
				runtime->platform->descriptor.user_data
			);
			if (result != FYUU_RESULT_SUCCESS) {
				return result;
			}
		}
		runtime->platform->initialized = true;
		if (runtime->descriptor.initialize) {
			runtime->descriptor.initialize(runtime, runtime->descriptor.user_data);
		}
		runtime->state = FYUU_RUNTIME_STATE_RUNNING;
		WriteRuntimeLog(*runtime, FYUU_LOG_LEVEL_INFO, "Runtime started");
		return FYUU_RESULT_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_RuntimeTick(Fyuu_Runtime* runtime) NOEXCEPT {
		if (!runtime) {
			return FYUU_RESULT_INVALID_ARGUMENT;
		}
		if (runtime->state == FYUU_RUNTIME_STATE_STOP_REQUESTED) {
			StopRuntime(*runtime);
			return FYUU_RESULT_SUCCESS;
		}
		if (runtime->state != FYUU_RUNTIME_STATE_RUNNING) {
			return FYUU_RESULT_INVALID_STATE;
		}
		auto close_requested = false;
		auto const result = runtime->platform->descriptor.pump_events(
			runtime->platform,
			runtime->platform->descriptor.user_data,
			&close_requested
		);
		if (result != FYUU_RESULT_SUCCESS) {
			WriteRuntimeLog(*runtime, FYUU_LOG_LEVEL_ERROR, "Platform event pump failed");
			return result;
		}
		if (close_requested) {
			auto const can_close = !runtime->descriptor.close_requested ||
				runtime->descriptor.close_requested(runtime, runtime->descriptor.user_data);
			if (can_close) {
				WriteRuntimeLog(*runtime, FYUU_LOG_LEVEL_INFO, "Close request accepted");
				runtime->state = FYUU_RUNTIME_STATE_STOP_REQUESTED;
			}
			else {
				WriteRuntimeLog(*runtime, FYUU_LOG_LEVEL_INFO, "Close request rejected");
			}
		}
		if (runtime->state == FYUU_RUNTIME_STATE_RUNNING && runtime->descriptor.tick) {
			runtime->descriptor.tick(runtime, runtime->descriptor.user_data);
		}
		if (runtime->state == FYUU_RUNTIME_STATE_STOP_REQUESTED) {
			StopRuntime(*runtime);
		}
		return FYUU_RESULT_SUCCESS;
	}

	LIB_API void LIB_CALL Fyuu_RuntimeRequestStop(Fyuu_Runtime* runtime) NOEXCEPT {
		if (runtime && runtime->state == FYUU_RUNTIME_STATE_RUNNING) {
			WriteRuntimeLog(*runtime, FYUU_LOG_LEVEL_INFO, "Stop requested");
			runtime->state = FYUU_RUNTIME_STATE_STOP_REQUESTED;
		}
	}

	LIB_API Fyuu_RuntimeState LIB_CALL Fyuu_RuntimeGetState(Fyuu_Runtime const* runtime) NOEXCEPT {
		return runtime ? runtime->state : FYUU_RUNTIME_STATE_STOPPED;
	}

	LIB_API void LIB_CALL Fyuu_RuntimeDestroy(Fyuu_Runtime* runtime) NOEXCEPT {
		if (!runtime) {
			return;
		}
		StopRuntime(*runtime);
		delete runtime;
	}

}
