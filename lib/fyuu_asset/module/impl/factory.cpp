module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <stdexcept>
#include <utility>

#include <filesystem>

#include <coroutine>
#endif // !defined(__cpp_lib_modules)

module fyuu_asset;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :asset;
import :asset_storage;
import :context;
import :factory;
import :operation;
import :serialization;

namespace fyuu_asset {
	Factory::Factory(std::string_view type, std::string_view name, Object const& fields) :
	    m_task{.m_operation = this, .Execute = ExecuteCreate}, m_type(type), m_name(name),
	    m_fields(fields) {
	}

	Factory::Factory(UUID const& uuid) :
	    m_task{.m_operation = this, .Execute = ExecuteLoadByUUID}, m_uuid(uuid) {
	}

	Factory::Factory(std::string_view type, std::string_view name) :
	    m_task{.m_operation = this, .Execute = ExecuteLoadByName}, m_type(type), m_name(name) {
	}

	void Factory::ExecuteCreate(OperationTask* submitted) noexcept {
		auto* factory = static_cast<Factory*>(submitted->m_operation);
		try {
			factory->m_uuid = GenerateAssetUUID();
			ValidateAssetIdentity(factory->m_uuid, factory->m_type, factory->m_name);
			auto const catalog_entry = CatalogEntry{
			    .m_uuid = factory->m_uuid,
			    .m_type = factory->m_type,
			    .m_name = factory->m_name
			};
			ValidateNewCatalogEntry(catalog_entry);
			auto const path = AssetStoragePath(factory->m_type, factory->m_name);
			if (std::filesystem::exists(path)) {
				throw std::invalid_argument{"Asset directory already contains asset.json"};
			}
			std::filesystem::create_directories(path.parent_path());
			SerializeAssetFields(path, factory->m_fields);
			RegisterCatalogEntry(catalog_entry);
		} catch (...) {
			factory->m_error = std::current_exception();
		}
	}

	void Factory::ExecuteLoadByUUID(OperationTask* submitted) noexcept {
		auto* factory = static_cast<Factory*>(submitted->m_operation);
		try {
			auto loaded = LoadAssetData(factory->m_uuid);
			factory->m_type = std::move(loaded.m_type);
			factory->m_name = std::move(loaded.m_name);
			factory->m_fields = std::move(loaded.m_fields);
		} catch (...) {
			factory->m_error = std::current_exception();
		}
	}

	void Factory::ExecuteLoadByName(OperationTask* submitted) noexcept {
		auto* factory = static_cast<Factory*>(submitted->m_operation);
		try {
			auto loaded = LoadAssetData(factory->m_type, factory->m_name);
			factory->m_uuid = loaded.m_uuid;
			factory->m_type = std::move(loaded.m_type);
			factory->m_name = std::move(loaded.m_name);
			factory->m_fields = std::move(loaded.m_fields);
		} catch (...) {
			factory->m_error = std::current_exception();
		}
	}

	void Factory::await_suspend(std::coroutine_handle<> continuation) {
		m_task.m_continuation = continuation;
		ScheduleAssetTask(&m_task);
	}

	Asset Factory::await_resume() {
		if (m_error) {
			std::rethrow_exception(m_error);
		}
		return Asset{m_uuid, m_type, m_name, std::move(m_fields)};
	}
} // namespace fyuu_asset
