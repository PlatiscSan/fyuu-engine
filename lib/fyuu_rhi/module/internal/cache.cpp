module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <cstdlib>
#include <vector>
#include <functional>
#include <string>
#include <fstream>

#include <chrono>
#include <atomic>
#include <mutex>
#include <thread>

#include <string_view>
#include <filesystem>

#include <source_location>
#include <ranges>
#include <span>
#include <format>
#endif // !defined(__cpp_lib_modules)
#include <boost/hash2/xxhash.hpp>
#if defined(__ANDROID__)
#include <android_native_app_glue.h>
#endif // defined(__ANDROID__)
module fyuu_rhi:cache;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :log;

namespace fs = std::filesystem;

namespace {

	using namespace fyuu_rhi;

	struct CacheEntry {
		fs::path path;
		std::size_t size = 0;
		fs::file_time_type last_modified;
		bool is_bundle = false;
	};

	fs::path s_cache_root;
	std::once_flag s_cache_init_flag;
	
	std::atomic_size_t s_max_cache_size_bytes{ 500u * 1024 * 1024 };
	std::mutex s_cleanup_mutex;
	std::mutex s_file_mutex;
	std::chrono::steady_clock::time_point s_last_check_time{};
	constexpr auto CHECK_COOLDOWN = std::chrono::seconds(30);

	std::size_t CalculateDirSize(fs::path const& dir) {
		std::size_t size = 0;
		std::error_code ec;
		for (auto const& entry : fs::recursive_directory_iterator(dir, ec)) {
			if (ec) {
				break;
			}
			if (entry.is_regular_file()) {
				size += entry.file_size();
			}
		}
		return size;
	}
	std::vector<CacheEntry> CollectEntries() {
		std::vector<CacheEntry> entries;
		if (s_cache_root.empty() || !fs::exists(s_cache_root)) {
			return entries;
		}
		std::error_code ec;
		for (auto const& entry : fs::directory_iterator(s_cache_root, ec)) {
			if (ec) {
				break;
			}
			if (!entry.is_regular_file()) {
				continue;
			}
			entries.push_back(
				{
					.path = entry.path(),
					.size = entry.file_size(),
					.last_modified = entry.last_write_time(),
					.is_bundle = false
				}
			);
		}
		auto bundles_dir = s_cache_root / "bundles";
		if (!fs::exists(bundles_dir)) return entries;
		for (auto const& entry : fs::directory_iterator(bundles_dir, ec)) {
			if (ec) {
				break;
			}
			if (!entry.is_directory()) {
				continue;
			}
			entries.push_back(
				{
					.path = entry.path(),
					.size = CalculateDirSize(entry.path()),
					.last_modified = entry.last_write_time(),
					.is_bundle = true
				}
			);
		}
		return entries;
	}

	void LazyCleanup() {
		std::size_t max_size = s_max_cache_size_bytes.load(std::memory_order_relaxed);
		if (max_size == 0) {
			return;
		}
		std::unique_lock lock(s_cleanup_mutex, std::try_to_lock);
		if (!lock.owns_lock()) {
			return;
		}

		auto now = std::chrono::steady_clock::now();
		if (now - s_last_check_time < CHECK_COOLDOWN) {
			return;
		}
		s_last_check_time = now;

		auto entries = CollectEntries();
		std::size_t total = 0;
		for (auto const& e : entries) {
			total += e.size;
		}
		if (total <= max_size) {
			return;
		}

		std::ranges::sort(entries, std::less{}, &CacheEntry::last_modified);

		for (auto const& e : entries) {
			if (total <= max_size) {
				break;
			}
			std::error_code ec;
			e.is_bundle ? fs::remove_all(e.path, ec): fs::remove(e.path, ec);
			if (!ec) {
				total -= e.size;
			}
		}
	}

	fs::path BuildSubdirectory(std::string_view app_name, Version const& app_ver, std::string_view engine_name, Version const& engine_ver) {
		std::string subdir = std::format(
			"{}/v{}_{}_{}/{}/v{}_{}_{}", 
			engine_name, engine_ver.major, engine_ver.minor, engine_ver.patch, 
			app_name, app_ver.major, app_ver.minor, app_ver.patch
		);
		return { subdir };
	}

}

namespace fyuu_rhi::cache {
	std::vector<std::byte> ReadFile(fs::path const& path) {
		std::unique_lock<std::mutex> lock(s_file_mutex);
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		if (!input) {
			return {};
		}
		auto size = input.tellg();
		if (size <= 0) {
			return {};
		}
		std::vector<std::byte> result(static_cast<std::size_t>(size));
		input.seekg(0);
		input.read(reinterpret_cast<char*>(result.data()), size);
		if (!input) {
			return {};
		}
		return result;
	}

	bool WriteFileAtomically(fs::path const& path, std::span<std::byte const> data) {
		std::unique_lock<std::mutex> lock(s_file_mutex);
		auto temporary = path;
		temporary += std::format(
			".tmp-{:x}",
			std::hash<std::thread::id>{}(std::this_thread::get_id())
		);
		{
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			output.write(
				reinterpret_cast<char const*>(data.data()),
				static_cast<std::streamsize>(data.size())
			);
			if (!output) {
				std::error_code error;
				fs::remove(temporary, error);
				return false;
			}
		}
		std::error_code error;
		fs::remove(path, error);
		error.clear();
		fs::rename(temporary, path, error);
		if (error) {
			fs::remove(temporary, error);
			return false;
		}
		return true;
	}

	void Initialize(
		std::string_view app_name, Version const& app_ver, 
		std::string_view engine_name, Version const& engine_ver
#if defined(__ANDROID__)
		, android_app* android_app
#endif // defined(__ANDROID__)
	) {
		std::call_once(
			s_cache_init_flag,
			[app_name, &app_ver, engine_name, &engine_ver 
#if defined(__ANDROID__)
			, android_app
#endif // defined(__ANDROID__)
			]() {
#if defined(_WIN32)
#if defined(__STDC_LIB_EXT1__)
				std::size_t required_size = 0u;
				std::string local_appdata;
				errno_t err = getenv_s(&required_size, nullptr, 0, "LOCALAPPDATA");
				if (err == 0 && required_size > 0u) {
					local_appdata.resize(required_size);
					err = getenv_s(&required_size, local_appdata.data(), required_size, "LOCALAPPDATA");
					s_cache_root = local_appdata;
				}
				else {
					s_cache_root = "./cache";
					log::Warning("cache::Initialize(): Failed to get LOCALAPPDATA environment variable, using current directory as cache root");
				}
#else
				char const* local_appdata = std::getenv("LOCALAPPDATA");
				if (local_appdata) {
					s_cache_root = local_appdata;
				}
				else {
					s_cache_root = "./cache";
					log::Warning("cache::Initialize(): Failed to get LOCALAPPDATA environment variable, using current directory as cache root");
				}
#endif // defined(__STDC_LIB_EXT1__)
#elif defined(__linux__)
#if defined(__STDC_LIB_EXT1__)
				std::size_t required_size = 0u;
				std::string xdg_cache_home;
				errno_t err = getenv_s(&required_size, nullptr, 0, "XDG_CACHE_HOME");
				if (err == 0 && required_size > 0u) {
					xdg_cache_home.resize(required_size);
					err = getenv_s(&required_size, xdg_cache_home.data(), required_size, "XDG_CACHE_HOME");
					s_cache_root = xdg_cache_home;
				}
				else {
					errno_t err = getenv_s(&required_size, nullptr, 0, "HOME");
					if (err == 0 && required_size > 0u) {
						std::string home;
						home.resize(required_size);
						err = getenv_s(&required_size, home.data(), required_size, "HOME");
						s_cache_root = home / ".cache";
					}
					else {
						s_cache_root = "./cache";
						log::Warning("cache::Initialize(): Failed to get HOME environment variable, using current directory as cache root");
					}
				}
#else
				char const* xdg_cache_home = std::getenv("XDG_CACHE_HOME");
				if (xdg_cache_home) {
					s_cache_root = xdg_cache_home;
				}
				else {
					char const* home = std::getenv("HOME");
					if (home) {
						s_cache_root = home;
						s_cache_root /= ".cache";
					}
					else {
						s_cache_root = "./cache";
						log::Warning("cache::Initialize(): Failed to get HOME environment variable, using current directory as cache root");
					}
				}
#endif // defined(__STDC_LIB_EXT1__)
#elif defined(__ANDROID__)
				if (android_app && android_app->activity && android_app->activity->internalDataPath) {
					fs::path data_path(android_app->activity->internalDataPath);
					s_cache_root = data_path.parent_path() / "cache";
				}
				else {
					s_cache_root = "./cache";
					log::Warning("cache::Initialize(): Failed to get Android internal data path, using current directory as cache root");
				}
#elif defined(__APPLE__)
#if defined(__STDC_LIB_EXT1__)
				std::size_t required_size = 0u;
				std::string home;
				errno_t err = getenv_s(&required_size, nullptr, 0, "HOME");
				if (err == 0 && required_size > 0u) {
					std::string home;
					home.resize(required_size);
					err = getenv_s(&required_size, home.data(), required_size, "HOME");
					s_cache_root = home;
					s_cache_root /= "Library/Caches";
				}
				else {
					s_cache_root = "./cache";
					log::Warning("cache::Initialize(): Failed to get HOME environment variable, using current directory as cache root");
				}
#else
				char const* home = std::getenv("HOME");
				if (home) {
					s_cache_root = home;
					s_cache_root /= "Library/Caches";
				}
				else {
					s_cache_root = "./cache";
					log::Warning("cache::Initialize(): Failed to get HOME environment variable, using current directory as cache root");
				}
#endif // defined(__STDC_LIB_EXT1__)
#endif // defined(_WIN32)
				s_cache_root /= BuildSubdirectory(app_name, app_ver, engine_name, engine_ver);
				fs::create_directories(s_cache_root);
			}
		);
	}

	fs::path GetCacheFilePath(std::string_view key) {

		LazyCleanup();

		auto dot_pos = key.rfind('.');
		std::string_view ext = dot_pos != std::string_view::npos && dot_pos != 0 ? key.substr(dot_pos) : std::string_view{};
		boost::hash2::xxhash_64 hasher;
		hasher.update(key.data(), key.size());
		std::uint64_t hash = hasher.result();
		std::string filename = std::format("{:016x}", hash);
		filename += ext.empty() ? ".bin" : ext;
		auto path = s_cache_root / filename;

		std::error_code ec;
		if (fs::exists(path, ec)) {
			fs::last_write_time(path, std::chrono::file_clock::now(), ec);
		}

		return path;

	}

	fs::path GetCacheDirectory(std::string_view key) {

		LazyCleanup();

		boost::hash2::xxhash_64 hasher;
		hasher.update(key.data(), key.size());
		std::uint64_t hash = hasher.result();
		std::string dirname = std::format("{:016x}", hash);
		fs::path dir = s_cache_root / "bundles" / dirname;
		fs::create_directories(dir);

		std::error_code ec;
		fs::last_write_time(dir, std::chrono::file_clock::now(), ec);

		return dir;

	}

	void SetMaxCacheSize(std::size_t max_bytes) {
		s_max_cache_size_bytes.store(max_bytes, std::memory_order::relaxed);
	}

}