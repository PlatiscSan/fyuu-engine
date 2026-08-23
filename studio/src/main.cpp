#include <version>
#if !defined(__cpp_lib_modules)
#include <string_view>
#endif // !defined(__cpp_lib_modules)

import fyuu_engine;
import fyuu_studio;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

int main(int argc, char** argv) {
	try {
		auto backend = fyuu_studio::DefaultRenderBackend();
		for (auto index = 1; index < argc; ++index) {
			std::string_view const argument{ argv[index] };
			static constexpr std::string_view Prefix = "--rhi=";
			if (argument.starts_with(Prefix)) {
				backend = fyuu_studio::ParseRenderBackend(argument.substr(Prefix.size()));
			}
		}
		fyuu_studio::Run(backend);
		return 0;
	}
	catch (fyuu_engine::Error const&) {
		return 1;
	}
}
