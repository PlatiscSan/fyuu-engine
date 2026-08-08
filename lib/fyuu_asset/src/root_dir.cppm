module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <mutex>

#include <shared_mutex>
#include <filesystem>
#endif // !defined(__cpp_lib_modules)
#if !defined(__cpp_lib_reflection)
#endif // !defined(__cpp_lib_reflection)
export module fyuu_asset:base_asset;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)

namespace fs = std::filesystem;

namespace {
	fs::path s_root_dir = "./asset";
	std::shared_mutex s_mutex;
}

namespace fyuu_asset {

	export fs::path GetAbsolutePath(fs::path const& relative) {
		std::shared_lock<std::shared_mutex> lock(s_mutex);
		if (relative.is_absolute()) {
			throw std::runtime_error("Incorrect relative format");
		}
		return fs::absolute(s_root_dir / relative);
	}

	export fs::path GetPath(fs::path const& relative) {
		std::shared_lock<std::shared_mutex> lock(s_mutex);
		if (relative.is_absolute()) {
			throw std::runtime_error("Incorrect relative format");
		}
		return s_root_dir / relative;
	}

	export void SetRoot(fs::path const& path) {
		std::lock_guard<std::shared_mutex> lock(s_mutex);
		s_root_dir = path;
	}

}
