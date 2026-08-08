module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <stdexcept>
#include <utility>
#include <mutex>
#include <filesystem>
#endif // !defined(__cpp_lib_modules)

export module fyuu_engine:asset;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_asset;

namespace {

	std::mutex s_asset_store_mutex;

}

export namespace fyuu_engine {

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

}
