module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <array>
#include <exception>
#include <filesystem>
#include <memory>
#include <print>
#include <source_location>
#include <string_view>
#endif // !defined(__cpp_lib_modules)
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include "fyuu_log.h"

module fyuu_engine;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :log;

namespace fs = std::filesystem;

namespace fyuu_engine::log {

	void Initialize() noexcept {
		try {
			fs::create_directories("logs");
			spdlog::init_thread_pool(8192, 1);
			auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
			auto engine_rotating_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
				"logs/engine.log",
				1024 * 1024 * 5,
				3
			);
			auto app_rotating_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
				"logs/app.log",
				1024 * 1024 * 5,
				3
			);
			std::array file_sinks = { engine_rotating_sink, app_rotating_sink };
			for (auto& sink : file_sinks) {
				sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
			}
			auto engine_logger = std::make_shared<spdlog::logger>(
				"Engine",
				spdlog::sinks_init_list({ console_sink, engine_rotating_sink })
			);
			auto app_logger = std::make_shared<spdlog::logger>(
				"App",
				spdlog::sinks_init_list({ console_sink, app_rotating_sink })
			);
			spdlog::register_logger(engine_logger);
			spdlog::register_logger(app_logger);
#if defined(NDEBUG)
			engine_logger->set_level(spdlog::level::info);
			app_logger->set_level(spdlog::level::info);
#else
			engine_logger->set_level(spdlog::level::debug);
			app_logger->set_level(spdlog::level::debug);
#endif // defined(NDEBUG)
			engine_logger->flush_on(spdlog::level::err);
			app_logger->flush_on(spdlog::level::err);
		}
		catch (spdlog::spdlog_ex const& exception) {
			std::println("log::Initialize() error occurred: {}", exception.what());
		}
		catch (std::exception const& exception) {
			std::println("log::Initialize() error occurred: {}", exception.what());
		}
		catch (...) {
			std::println("log::Initialize() error occurred: unknown exception");
		}
	}

	void Shutdown() noexcept {
		spdlog::shutdown();
	}

	void Trace(std::string_view message, std::source_location const& location) noexcept {
		auto logger = spdlog::get("Engine");
		if (logger) logger->trace("file: {}({},{}) '{}': {}", location.file_name(), location.line(), location.column(), location.function_name(), message);
	}

	void Debug(std::string_view message, std::source_location const& location) noexcept {
		auto logger = spdlog::get("Engine");
		if (logger) logger->debug("file: {}({},{}) '{}': {}", location.file_name(), location.line(), location.column(), location.function_name(), message);
	}

	void Info(std::string_view message, std::source_location const& location) noexcept {
		auto logger = spdlog::get("Engine");
		if (logger) logger->info("file: {}({},{}) '{}': {}", location.file_name(), location.line(), location.column(), location.function_name(), message);
	}

	void Warning(std::string_view message, std::source_location const& location) noexcept {
		auto logger = spdlog::get("Engine");
		if (logger) logger->warn("file: {}({},{}) '{}': {}", location.file_name(), location.line(), location.column(), location.function_name(), message);
	}

	void Error(std::string_view message, std::source_location const& location) noexcept {
		auto logger = spdlog::get("Engine");
		if (logger) logger->error("file: {}({},{}) '{}': {}", location.file_name(), location.line(), location.column(), location.function_name(), message);
	}

	void Fatal(std::string_view message, std::source_location const& location) noexcept {
		auto logger = spdlog::get("Engine");
		if (logger) logger->critical("file: {}({},{}) '{}': {}", location.file_name(), location.line(), location.column(), location.function_name(), message);
	}

}

extern "C" {

	LIB_API void LIB_CALL Fyuu_Trace(char const* message, char const* file, uint_least32_t line, char const* function) {
		auto logger = spdlog::get("App");
		if (logger) logger->trace("file: {}({}) '{}': {}", file, line, function, message);
	}

	LIB_API void LIB_CALL Fyuu_Debug(char const* message, char const* file, uint_least32_t line, char const* function) {
		auto logger = spdlog::get("App");
		if (logger) logger->debug("file: {}({}) '{}': {}", file, line, function, message);
	}

	LIB_API void LIB_CALL Fyuu_Info(char const* message, char const* file, uint_least32_t line, char const* function) {
		auto logger = spdlog::get("App");
		if (logger) logger->info("file: {}({}) '{}': {}", file, line, function, message);
	}

	LIB_API void LIB_CALL Fyuu_Warning(char const* message, char const* file, uint_least32_t line, char const* function) {
		auto logger = spdlog::get("App");
		if (logger) logger->warn("file: {}({}) '{}': {}", file, line, function, message);
	}

	LIB_API void LIB_CALL Fyuu_Error(char const* message, char const* file, uint_least32_t line, char const* function) {
		auto logger = spdlog::get("App");
		if (logger) logger->error("file: {}({}) '{}': {}", file, line, function, message);
	}

	LIB_API void LIB_CALL Fyuu_Fatal(char const* message, char const* file, uint_least32_t line, char const* function) {
		auto logger = spdlog::get("App");
		if (logger) logger->critical("file: {}({}) '{}': {}", file, line, function, message);
	}

}
