module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <exception>
#include <stdexcept>
#include <utility>

#include <vector>

#include <algorithm>

#include <string>

#include <unordered_map>

#include <mutex>

#include <string_view>

#include <filesystem>

#include <coroutine>
#endif // !defined(__cpp_lib_modules)
#include <nlohmann/json.hpp>

module fyuu_asset;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :import_file_system;
import :importer;
import :importer_impl;
import :operation;
import :value;

namespace {
	[[nodiscard]] auto& ImporterRegistry() {
		static std::unordered_map<std::string, fyuu_asset::ImportFunction> importers;
		return importers;
	}

	[[nodiscard]] auto& ImporterRegistryMutex() {
		static std::mutex mutex;
		return mutex;
	}

	[[nodiscard]] fyuu_asset::ImportFunction ResolveImporter(std::string_view type) {
		std::lock_guard lock{ImporterRegistryMutex()};
		auto const found = ImporterRegistry().find(std::string{type});
		if (found == ImporterRegistry().end()) {
			throw std::out_of_range{"Asset type has no registered importer"};
		}
		return found->second;
	}

	[[nodiscard]] std::vector<std::string> ReadPackageEntries(
	    fyuu_asset::ImportFileSystem const& file_system
	) {
		if (!file_system.Exists("asset.json")) {
			return fyuu_asset::FindImportEntries(file_system);
		}
		auto const bytes = file_system.Read("asset.json");
		auto const* first = reinterpret_cast<char const*>(bytes.data());
		auto const document = nlohmann::json::parse(first, first + bytes.size());
		if (!document.is_object()) {
			throw std::invalid_argument{"Import package asset.json must contain an object"};
		}
		std::vector<std::string> entries;
		if (document.contains("entry") && document["entry"].is_string()) {
			entries.emplace_back(document["entry"].get<std::string>());
		} else if (document.contains("entries") && document["entries"].is_array()) {
			for (auto const& value : document["entries"]) {
				if (!value.is_string()) {
					throw std::invalid_argument{"Import package entries must contain only strings"};
				}
				entries.emplace_back(value.get<std::string>());
			}
		} else {
			throw std::invalid_argument{"Import package asset.json must contain entry or entries"};
		}
		if (entries.empty() || std::ranges::any_of(entries, [](auto const& entry) {
			    return entry.empty();
		    })) {
			throw std::invalid_argument{"Import package entries must not be empty"};
		}
		return entries;
	}

	[[nodiscard]] fyuu_asset::Object ImportSource(std::filesystem::path const& source) {
		std::filesystem::path root;
		std::vector<std::string> entries;
		if (std::filesystem::is_regular_file(source) && source.extension() != ".zip") {
			root = source.parent_path();
			entries.emplace_back(fyuu_asset::ToVirtualPath(source.filename()));
		} else {
			root = source;
		}

		fyuu_asset::ImportFileSystem file_system{root};
		if (entries.empty()) {
			entries = ReadPackageEntries(file_system);
		}
		if (entries.size() == 1u) {
			return fyuu_asset::ImportAsset(file_system, entries.front(), {});
		}

		fyuu_asset::Array models;
		models.reserve(entries.size());
		for (std::size_t index = 0u; index < entries.size(); ++index) {
			auto model = fyuu_asset::ImportAsset(file_system, entries[index], {});
			model.emplace("name", fyuu_asset::Value{"model_" + std::to_string(index)});
			models.emplace_back(std::move(model));
		}
		return fyuu_asset::Object{{"models", fyuu_asset::Value{std::move(models)}}};
	}
} // namespace

namespace fyuu_asset {
	Importer::Importer(std::string_view type, std::filesystem::path const& source) :
	    m_task{.m_operation = this, .Execute = Execute},
	    m_source(source),
	    Import(ResolveImporter(type)) {
	}

	void Importer::Register(std::string_view type, ImportFunction Import) {
		if (type.empty() || type.contains('\0') || !Import) {
			throw std::invalid_argument{"Importer registration is invalid"};
		}
		std::lock_guard lock{ImporterRegistryMutex()};
		if (!ImporterRegistry().emplace(std::string{type}, Import).second) {
			throw std::invalid_argument{"Asset type already has a registered importer"};
		}
	}

	void Importer::RegisterBuiltIn(std::string_view type) {
		Register(type, ImportSource);
	}

	void Importer::Execute(OperationTask* submitted) noexcept {
		auto* importer = static_cast<Importer*>(submitted->m_operation);
		try {
			importer->m_result = importer->Import(importer->m_source);
		} catch (...) {
			importer->m_error = std::current_exception();
		}
	}

	void Importer::await_suspend(std::coroutine_handle<> continuation) {
		m_task.m_continuation = continuation;
		ScheduleAssetTask(&m_task);
	}

	Object Importer::await_resume() {
		if (m_error) {
			std::rethrow_exception(m_error);
		}
		return std::move(m_result);
	}
} // namespace fyuu_asset
