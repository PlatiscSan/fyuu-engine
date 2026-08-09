module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <atomic>
#include <stdexcept>
#include <string>
#include <filesystem>
#endif // !defined(__cpp_lib_modules)
#include "fyuu_asset.h"

module fyuu_engine:asset_api;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :asset;

struct Fyuu_AssetStore_T {
	std::atomic_size_t references = 1u;
	fyuu_engine::AssetStore store;

	explicit Fyuu_AssetStore_T(std::filesystem::path const& root)
		: store(root) {
	}
};

namespace fyuu_engine::api {

	void RetainAssetStore(Fyuu_AssetStore store) noexcept {
		store->references.fetch_add(1u, std::memory_order_relaxed);
	}

	void ReleaseAssetStore(Fyuu_AssetStore store) noexcept {
		if (store->references.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
			delete store;
		}
	}

}

extern "C" {

	LIB_API Fyuu_Result LIB_CALL Fyuu_AssetStoreCreate(
		Fyuu_AssetStoreDescriptor const* descriptor,
		Fyuu_AssetStore* output
	) NOEXCEPT {
		if (!descriptor || !output || !descriptor->root.data || descriptor->root.size == 0u) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		*output = nullptr;

		try {
			auto const* root_begin = reinterpret_cast<char8_t const*>(descriptor->root.data);
			std::u8string root_text(root_begin, root_begin + descriptor->root.size);
			std::filesystem::path root{ root_text };
			*output = new Fyuu_AssetStore_T{ root };
			return FYUU_SUCCESS;
		}
		catch (std::invalid_argument const&) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		catch (std::logic_error const&) {
			return FYUU_ERROR_INVALID_STATE;
		}
		catch (std::filesystem::filesystem_error const&) {
			return FYUU_ERROR_IO;
		}
		catch (...) {
			return FYUU_ERROR_OPERATION_FAILED;
		}
	}

	LIB_API void LIB_CALL Fyuu_AssetStoreRelease(Fyuu_AssetStore store) NOEXCEPT {
		if (!store) {
			return;
		}

		fyuu_engine::api::ReleaseAssetStore(store);
	}

}
