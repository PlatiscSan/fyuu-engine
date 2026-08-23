module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <new>
#endif // !defined(__cpp_lib_modules)
#include "fyuu_log.h"

module fyuu_engine:log_entry;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

extern "C" {

	struct Fyuu_Logger {
		Fyuu_LoggerDescriptor descriptor;
	};

}

namespace {

	bool ValidLoggerDescriptor(Fyuu_LoggerDescriptor const& descriptor) noexcept {
		return descriptor.struct_size >= sizeof(Fyuu_LoggerDescriptor) &&
			descriptor.ABI_version == FYUU_ABI_VERSION &&
			descriptor.sink;
	}

	bool ValidLogLevel(Fyuu_LogLevel level) noexcept {
		return level >= FYUU_LOG_LEVEL_TRACE &&
			level <= FYUU_LOG_LEVEL_FATAL;
	}

	bool ValidLogRecord(Fyuu_LogRecord const& record) noexcept {
		return ValidLogLevel(record.level) &&
			(record.category_data || record.category_size == 0) &&
			(record.message_data || record.message_size == 0);
	}

}

extern "C" {

	LIB_API Fyuu_Result LIB_CALL Fyuu_LoggerCreate(
		Fyuu_LoggerDescriptor const* descriptor,
		Fyuu_Logger** output
	) NOEXCEPT {
		if (!descriptor || !output || !ValidLoggerDescriptor(*descriptor)) {
			return FYUU_RESULT_INVALID_ARGUMENT;
		}
		*output = nullptr;
		auto logger = new (std::nothrow) Fyuu_Logger{ *descriptor };
		if (!logger) {
			return FYUU_RESULT_OUT_OF_MEMORY;
		}
		*output = logger;
		return FYUU_RESULT_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_LoggerWrite(
		Fyuu_Logger* logger,
		Fyuu_LogRecord const* record
	) NOEXCEPT {
		if (!logger || !record || !ValidLogRecord(*record)) {
			return FYUU_RESULT_INVALID_ARGUMENT;
		}
		return logger->descriptor.sink(
			logger,
			logger->descriptor.user_data,
			record
		);
	}

	LIB_API void LIB_CALL Fyuu_LoggerDestroy(Fyuu_Logger* logger) NOEXCEPT {
		delete logger;
	}

}
