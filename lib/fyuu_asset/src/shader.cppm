module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <string>
#endif
#if !defined(__cpp_lib_reflection)
#include <boost/describe.hpp>
#endif

export module fyuu_asset:shader;

#if defined(__cpp_lib_modules)
import std;
#endif

export namespace fyuu_asset {

	// One Slang source module. Entry points, stages, macros and optimization
	// options describe a pipeline program and intentionally do not live here.
	struct Shader {

		std::string name;
		std::string source;

		[[nodiscard]] bool Valid() const noexcept {
			return !name.empty() && !source.empty();
		}
	};

#if !defined(__cpp_lib_reflection)
	BOOST_DESCRIBE_STRUCT(
		Shader,
		(),
		(name, source)
	)
#endif

}
