#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <iostream>

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
			std::string_view const argument{argv[index]};
			static constexpr std::string_view Prefix = "--rhi=";
			if (argument.starts_with(Prefix)) {
				backend = fyuu_studio::ParseRenderBackend(argument.substr(Prefix.size()));
			}
		}
		fyuu_studio::Run(backend);
		return 0;
	} catch (fyuu_engine::Error const& error) {
		std::cerr << "FyuuEngine error: " << error.what() << '\n';
		return 1;
	} catch (std::exception const& error) {
		// Keep UI/model invariant failures observable. Previously every standard
		// exception reached terminate and Windows reported only 0xc0000409.
		std::cerr << "Unhandled standard exception: " << error.what() << '\n';
		return 2;
	} catch (...) {
		std::cerr << "Unhandled non-standard exception.\n";
		return 1;
	}
}
