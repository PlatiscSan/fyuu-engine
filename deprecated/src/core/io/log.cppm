module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <source_location>
#include <string_view>
#endif // !defined(__cpp_lib_modules)

export module fyuu_engine:log;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

export namespace fyuu_engine::log {

	void Initialize() noexcept;
	void Shutdown() noexcept;

	void Trace(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept;

	void Debug(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept;

	void Info(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept;

	void Warning(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept;

	void Error(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept;

	void Fatal(
		std::string_view message,
		std::source_location const& location = std::source_location::current()
	) noexcept;

}
