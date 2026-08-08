module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <unordered_map>
#include <memory>

#include <mutex>

#include <concepts>
#if defined(__cpp_lib_reflection)
#include <meta>
#endif // defined(__cpp_lib_reflection)
#endif // !defined(__cpp_lib_modules)
#include <boost/uuid.hpp>
module fyuu_asset:registry;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :base_asset;

namespace {

	using namespace fyuu_asset;

	std::unordered_map<boost::uuids::uuid, std::weak_ptr<BaseAsset>> s_asset_registry;
	std::mutex s_registry_mutex;

}

namespace fyuu_asset::registry {

	bool RegisterAsset(std::shared_ptr<BaseAsset> const& asset) {
		std::lock_guard<std::mutex> lock(s_registry_mutex);
		auto [it, inserted] = s_asset_registry.emplace(asset->GetID(), std::weak_ptr<BaseAsset>(asset));
		return inserted;
	}

	bool UnregisterAsset(boost::uuids::uuid const& id) {
		std::lock_guard<std::mutex> lock(s_registry_mutex);
		return s_asset_registry.erase(id) > 0;
	}

}
