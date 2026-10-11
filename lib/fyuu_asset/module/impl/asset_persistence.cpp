module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>

#include <string>

#include <filesystem>
#endif // !defined(__cpp_lib_modules)

module fyuu_asset;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :asset;
import :asset_storage;
import :context;
import :operation;
import :serialization;
import :uuid;
import :value;

namespace {
	/** @brief Owns an asset snapshot until the worker persists it. */
	struct SaveTask final {
		fyuu_asset::OperationTask m_task; ///< Intrusive node submitted to the asset worker.
		fyuu_asset::UUID m_uuid;          ///< Stable asset identity.
		std::string m_type;               ///< Runtime asset type.
		std::string m_name;               ///< Catalog asset name.
		fyuu_asset::Object m_fields;      ///< Final dynamic field snapshot.
	};

	/** @brief Persists and releases one detached asset snapshot. */
	void ExecuteSave(fyuu_asset::OperationTask* submitted) noexcept {
		auto task = std::unique_ptr<SaveTask>{static_cast<SaveTask*>(submitted->m_operation)};
		try {
			try {
				auto const existing = fyuu_asset::FindCatalogEntry(task->m_uuid);
				if (existing.m_type != task->m_type || existing.m_name != task->m_name) {
					throw std::invalid_argument{"Asset UUID conflicts with the catalog"};
				}
			} catch (std::out_of_range const&) {
				return;
			}
			auto path = fyuu_asset::AssetStoragePath(task->m_type, task->m_name);
			auto const legacy = fyuu_asset::GetAssetRoot() /
			    fyuu_asset::FromVirtualPath(task->m_type) /
			    fyuu_asset::FromVirtualPath(task->m_name + ".json");
			if (!std::filesystem::exists(path) && std::filesystem::exists(legacy)) {
				path = legacy;
			}
			std::filesystem::create_directories(path.parent_path());
			fyuu_asset::SerializeAssetFields(path, task->m_fields);
			fyuu_asset::RegisterCatalogEntry(
			    fyuu_asset::CatalogEntry{
			        .m_uuid = task->m_uuid,
			        .m_type = task->m_type,
			        .m_name = task->m_name
			    }
			);
		} catch (...) {
		}
	}
} // namespace

namespace fyuu_asset {
	void Asset::QueuePersistence() noexcept {
		if (!m_owns_persistence) {
			return;
		}
		m_owns_persistence = false;
		auto* task = new (std::nothrow) SaveTask{
		    .m_task = {.Execute = ExecuteSave},
		    .m_uuid = m_uuid,
		    .m_type = std::move(m_type),
		    .m_name = std::move(m_name),
		    .m_fields = std::move(m_fields)
		};
		if (!task) {
			return;
		}
		task->m_task.m_operation = task;
		try {
			ScheduleAssetTask(&task->m_task);
		} catch (...) {
			delete task;
		}
	}
} // namespace fyuu_asset
