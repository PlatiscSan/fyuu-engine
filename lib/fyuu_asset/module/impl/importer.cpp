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
	[[nodiscard]] auto& ImportProtocols() {
		static std::unordered_map<std::string, fyuu_asset::ImportProtocol> protocols;
		return protocols;
	}

	[[nodiscard]] auto& ImportProtocolsMutex() {
		static std::mutex mutex;
		return mutex;
	}

	[[nodiscard]] fyuu_asset::ImportProtocol ResolveImportProtocol(std::string_view type) {
		std::lock_guard lock{ImportProtocolsMutex()};
		auto const found = ImportProtocols().find(std::string{type});
		if (found == ImportProtocols().end()) {
			throw std::out_of_range{"Asset type has no registered import protocol"};
		}
		return found->second;
	}

	[[nodiscard]] std::vector<std::string> ReadDeclaredEntries(
	    fyuu_asset::ImportFileSystem const& file_system
	) {
		if (!file_system.Exists("asset.json")) {
			return {};
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

	[[nodiscard]] fyuu_asset::Object ParseBuiltIn(
	    fyuu_asset::ImportFileSystem const& file_system
	) {
		auto const files = file_system.ListFiles();
		if (files.size() == 1u) {
			return fyuu_asset::ImportAsset(file_system, files.front(), {});
		}
		auto entries = std::ranges::find(files, "asset.json") != files.end()
		    ? ReadDeclaredEntries(file_system)
		    : std::vector<std::string>{};
		if (entries.empty()) {
			entries = fyuu_asset::FindImportEntries(file_system);
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
	void Importer::Register(std::string_view type, ImportProtocol const& protocol) {
		if (type.empty() || type.contains('\0') || !protocol) {
			throw std::invalid_argument{"Import protocol type is invalid"};
		}
		std::lock_guard lock{ImportProtocolsMutex()};
		if (!ImportProtocols().emplace(std::string{type}, protocol).second) {
			throw std::invalid_argument{"Asset type already has an import protocol"};
		}
	}

	void Importer::RegisterBuiltIn(std::string_view type) {
		Register(type, ParseBuiltIn);
	}

	Importer::Importer(std::string_view type, std::filesystem::path const& source) :
	    m_task{.m_operation = this, .Execute = Execute}, m_source(source),
	    m_protocol(ResolveImportProtocol(type)) {
	}

	void Importer::Execute(OperationTask* submitted) noexcept {
		auto* importer = static_cast<Importer*>(submitted->m_operation);
		try {
			ImportFileSystem file_system{importer->m_source};
			importer->m_result = importer->m_protocol(file_system);
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
