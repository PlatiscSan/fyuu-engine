module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <string_view>
#endif // !defined(__cpp_lib_modules)
#include "fyuu_log.h"

module fyuu_engine:log;

import fyuu_engine;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace {

	void ThrowIfLogFailed(Fyuu_Result const& result) {
		switch (result) {
		case FYUU_RESULT_SUCCESS:
			return;
		case FYUU_RESULT_INVALID_ARGUMENT:
			throw fyuu_engine::Error{ fyuu_engine::Result::InvalidArgument, "Invalid log argument" };
		case FYUU_RESULT_OUT_OF_MEMORY:
			throw fyuu_engine::Error{ fyuu_engine::Result::OutOfMemory, "Unable to allocate Logger" };
		case FYUU_RESULT_LOG_ERROR:
			throw fyuu_engine::Error{ fyuu_engine::Result::LogError, "Log sink failed" };
		default:
			throw fyuu_engine::Error{ fyuu_engine::Result::UnknownError, "Unknown logging error" };
		}
	}

}

namespace fyuu_engine {

	LogSink::~LogSink() noexcept = default;

	Logger::Logger(LogSink& sink)
		: m_sink(&sink) {
		Fyuu_LoggerDescriptor const descriptor{
			sizeof(Fyuu_LoggerDescriptor),
			FYUU_ABI_VERSION,
			this,
			WriteThunk
		};
		auto const result = Fyuu_LoggerCreate(&descriptor, &m_handle);
		if (result != FYUU_RESULT_SUCCESS) {
			m_sink = nullptr;
		}
		ThrowIfLogFailed(result);
	}

	Logger::~Logger() noexcept {
		Fyuu_LoggerDestroy(m_handle);
	}

	void Logger::Write(LogRecord const& record) {
		Fyuu_LogRecord const ABI_record{
			static_cast<Fyuu_LogLevel>(record.level),
			record.category.data(),
			record.category.size(),
			record.message.data(),
			record.message.size()
		};
		auto const result = Fyuu_LoggerWrite(m_handle, &ABI_record);
		if (m_pending_exception) {
			auto const exception = m_pending_exception;
			m_pending_exception = nullptr;
			std::rethrow_exception(exception);
		}
		ThrowIfLogFailed(result);
	}

	void Logger::Write(
		LogLevel const& level,
		std::string_view const& category,
		std::string_view const& message
	) {
		Write(LogRecord{ level, category, message });
	}

	bool Logger::Valid() const noexcept {
		return m_handle;
	}

}
