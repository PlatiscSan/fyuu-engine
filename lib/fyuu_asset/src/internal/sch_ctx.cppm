module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <concepts>
#if defined(__cpp_lib_reflection)
#include <meta>
#endif // defined(__cpp_lib_reflection)
#endif // !defined(__cpp_lib_modules)
module fyuu_asset:scheduler_context;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :asset_manager;

namespace fyuu_asset::execution {

	class SchedulerContext {

	};

}