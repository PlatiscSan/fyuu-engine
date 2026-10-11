module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>

#include <string>

#include <string_view>

#include <filesystem>

#include <coroutine>
#endif // !defined(__cpp_lib_modules)

module fyuu_asset;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :asset_storage;
import :context;
import :deleter;
import :operation;
import :uuid;

namespace {
	/** @brief Deletes one catalogued asset and its independent storage directory. */
	[[nodiscard]] fyuu_asset::UUID DeleteAssetData(fyuu_asset::CatalogEntry const& identity) {
		auto const path = fyuu_asset::AssetStoragePath(identity.m_type, identity.m_name);
		auto const legacy = fyuu_asset::GetAssetRoot() /
		    fyuu_asset::FromVirtualPath(identity.m_type) /
		    fyuu_asset::FromVirtualPath(identity.m_name + ".json");
		fyuu_asset::RemoveCatalogEntry(identity.m_uuid);
		if (std::filesystem::exists(path)) {
			std::filesystem::remove_all(path.parent_path());
		} else {
			std::filesystem::remove(legacy);
		}
		return identity.m_uuid;
	}
} // namespace

namespace fyuu_asset {
	Deleter::Deleter(UUID const& uuid) noexcept :
	    m_task{.m_operation = this, .Execute = ExecuteByUUID}, m_uuid(uuid) {
	}

	Deleter::Deleter(std::string_view type, std::string_view name) :
	    m_task{.m_operation = this, .Execute = ExecuteByName}, m_type(type), m_name(name) {
	}

	void Deleter::ExecuteByUUID(OperationTask* submitted) noexcept {
		auto* deleter = static_cast<Deleter*>(submitted->m_operation);
		try {
			deleter->m_uuid = DeleteAssetData(FindCatalogEntry(deleter->m_uuid));
		} catch (...) {
			deleter->m_error = std::current_exception();
		}
	}

	void Deleter::ExecuteByName(OperationTask* submitted) noexcept {
		auto* deleter = static_cast<Deleter*>(submitted->m_operation);
		try {
			deleter->m_uuid = DeleteAssetData(FindCatalogEntry(deleter->m_type, deleter->m_name));
		} catch (...) {
			deleter->m_error = std::current_exception();
		}
	}

	void Deleter::await_suspend(std::coroutine_handle<> continuation) {
		m_task.m_continuation = continuation;
		ScheduleAssetTask(&m_task);
	}

	UUID Deleter::await_resume() {
		if (m_error) {
			std::rethrow_exception(m_error);
		}
		return m_uuid;
	}
} // namespace fyuu_asset
