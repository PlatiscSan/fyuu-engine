module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <string>
#include <stdexcept>
#include <utility>
#include <mutex>
#include <filesystem>
#include <string_view>
#include <vector>
#endif // !defined(__cpp_lib_modules)

export module fyuu_engine:asset;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
export import fyuu_asset;

namespace {

	std::mutex s_asset_store_mutex;

}

export namespace fyuu_engine {
	struct AssetEntry {
		fyuu_asset::UUID id;
		std::string name;
	};

	class AssetStore {
	private:
		inline static AssetStore* s_active_store = nullptr;
		std::filesystem::path m_root;

	public:
		explicit AssetStore(std::filesystem::path const& root) {
			if (root.empty()) {
				throw std::invalid_argument("Asset root cannot be empty");
			}

			auto absolute_root = std::filesystem::absolute(root);
			std::lock_guard lock(s_asset_store_mutex);
			if (s_active_store) {
				throw std::logic_error("Only one AssetStore can be active");
			}

			fyuu_asset::SetRoot(absolute_root);
			m_root = std::move(absolute_root);
			s_active_store = this;
		}

		AssetStore(AssetStore const&) = delete;
		AssetStore& operator=(AssetStore const&) = delete;
		AssetStore(AssetStore&&) = delete;
		AssetStore& operator=(AssetStore&&) = delete;

		~AssetStore() noexcept {
			std::lock_guard lock(s_asset_store_mutex);
			if (s_active_store == this) {
				s_active_store = nullptr;
			}
		}

		[[nodiscard]] std::filesystem::path Root() const noexcept {
			return m_root;
		}
	};

	[[nodiscard]] std::vector<AssetEntry> DiscoverAssets(
		AssetStore const& store,
		std::string_view category
	) {
		if (category.empty() || std::ranges::any_of(category, [](char character) {
			return character == '/' || character == '\\';
		})) {
			throw std::invalid_argument("Asset category must be a single directory name");
		}

		std::vector<AssetEntry> assets;
		auto const directory = store.Root() / category;
		std::error_code error;
		if (!std::filesystem::exists(directory, error)) {
			if (error) {
				throw std::filesystem::filesystem_error(
					"Failed to inspect asset directory",
					directory,
					error
				);
			}
			return assets;
		}

		for (std::filesystem::directory_iterator iterator(directory, error), end;
			iterator != end;
			iterator.increment(error)) {
			if (error) {
				throw std::filesystem::filesystem_error(
					"Failed to enumerate assets",
					directory,
					error
				);
			}
			if (!iterator->is_regular_file() || iterator->path().extension() != ".json") {
				continue;
			}
			try {
				auto name = iterator->path().stem().string();
				assets.push_back(AssetEntry{
					.id = fyuu_asset::UUID::Parse(name),
					.name = std::move(name)
				});
			}
			catch (...) {
			}
		}
		if (error) {
			throw std::filesystem::filesystem_error(
				"Failed to enumerate assets",
				directory,
				error
			);
		}
		std::ranges::sort(assets, {}, &AssetEntry::name);
		return assets;
	}

}
