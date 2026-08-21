module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <atomic>
#include <string_view>
#include <source_location>
#endif // !defined(__cpp_lib_modules)
module fyuu_rhi:log;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :core;

namespace {
	std::atomic<fyuu_rhi::log::Sink*> s_sink = nullptr;
}

namespace fyuu_rhi::log {

	void SetSink(Sink* sink) noexcept {
		s_sink.store(sink, std::memory_order::release);
	}

	void Debug(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept {
		if (auto sink = s_sink.load(std::memory_order::acquire)) {
			sink->Write(Level::Debug, message, location);
		}
	}

	void Info(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept {
		if (auto sink = s_sink.load(std::memory_order::acquire)) {
			sink->Write(Level::Info, message, location);
		}
	}

	void Warning(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept {
		if (auto sink = s_sink.load(std::memory_order::acquire)) {
			sink->Write(Level::Warning, message, location);
		}
	}

	void Error(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept {
		if (auto sink = s_sink.load(std::memory_order::acquire)) {
			sink->Write(Level::Error, message, location);
		}
	}

	void Fatal(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept {
		if (auto sink = s_sink.load(std::memory_order::acquire)) {
			sink->Write(Level::Fatal, message, location);
		}
	}

} // namespace fyuu_rhi::log
