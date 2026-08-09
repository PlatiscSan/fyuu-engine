module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string_view>

#include <source_location>
#endif // !defined(__cpp_lib_modules)
export module fyuu_rhi:log;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
namespace fyuu_rhi::log {

	using LogFunction = void(*)(std::string_view, std::source_location const&);

	export LogFunction Trace;
	export LogFunction Info;
	export LogFunction Warning;
	export LogFunction Error;
	export LogFunction Fatal;

}