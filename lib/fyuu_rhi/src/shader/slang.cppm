module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#endif

#include <slang.h>
#include <slang-com-ptr.h>
#include <boost/hash2/xxhash.hpp>
#include <nlohmann/json.hpp>

#include "log.hpp"

module fyuu_rhi:slang;
#if defined(__cpp_lib_modules)
import std;
#endif
import :pipeline_types;
import :slang_pipeline_interface;
import :log;
import :cache_system;

namespace fs = std::filesystem;

namespace fyuu_rhi::shader {

	using namespace fyuu_rhi::pipeline;

	Slang::ComPtr<slang::IGlobalSession> SlangGlobalSession() {
		static std::once_flag once;
		static Slang::ComPtr<slang::IGlobalSession> session;
		static SlangResult creation_result = SLANG_FAIL;
		auto CreateGlobalSession = [&]() {
			creation_result = slang::createGlobalSession(session.writeRef());
		};
		std::call_once(once, CreateGlobalSession);
		if (SLANG_FAILED(creation_result) || !session) {
			throw std::runtime_error(std::format("Failed to create Slang global session ({})", creation_result));
		}
		return session;
	}

	struct SlangCompiledEntryPoint {
		PipelineStage stage;
		std::string name;
		std::vector<std::byte> code;
	};

	class SlangProgram {
	private:
		static constexpr std::uint32_t CACHE_SCHEMA_VERSION = 4u;

		std::vector<SlangCompiledEntryPoint> m_entry_points;
		SlangPipelineInterface m_interface;
		std::string m_reflection_json;

		static std::string Diagnostics(slang::IBlob* blob) {
			if (!blob || !blob->getBufferPointer() || blob->getBufferSize() == 0) {
				return {};
			}
			return { static_cast<char const*>(blob->getBufferPointer()), blob->getBufferSize() };
		}

		static void Check(SlangResult result, slang::IBlob* diagnostics, std::string_view operation) {
			auto message = Diagnostics(diagnostics);
			if (SLANG_FAILED(result)) {
				throw std::runtime_error(std::format("{} failed ({}): {}", operation, result, message));
			}
			if (!message.empty()) {
				LOG_WARNING(std::format("{}: {}", operation, message));
			}
		}

		template <class Hasher>
		static void HashString(Hasher& hash, std::string_view value) {
			auto size = static_cast<std::uint64_t>(value.size());
			hash.update(&size, sizeof(size));
			hash.update(value.data(), value.size());
		}

		static std::uint64_t HashBytes(std::span<std::byte const> bytes) {
			boost::hash2::xxhash_64 hash;
			hash.update(bytes.data(), bytes.size());
			return hash.result();
		}

		static std::uint64_t HashFile(fs::path const& path) {
			std::ifstream input(path, std::ios::binary);
			if (!input) {
				throw std::runtime_error(std::format("Cannot read Slang dependency '{}'", path.string()));
			}

			boost::hash2::xxhash_64 hash;
			std::array<char, 64u * 1024u> buffer;
			while (input) {
				input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
				auto count = input.gcount();
				if (count > 0) {
					hash.update(buffer.data(), static_cast<std::size_t>(count));
				}
			}
			if (!input.eof()) {
				throw std::runtime_error(std::format("Failed while reading Slang dependency '{}'", path.string()));
			}
			return hash.result();
		}

		static std::string BuildCacheKey(
			slang::TargetDesc const& target,
			SlangPipelineProgramDescriptor const& desc,
			std::string_view cache_tag
		) {
			boost::hash2::xxhash_64 hash;
			HashString(hash, "fyuu-rhi-slang-program");
			hash.update(&CACHE_SCHEMA_VERSION, sizeof(CACHE_SCHEMA_VERSION));
			HashString(hash, SlangGlobalSession()->getBuildTagString());
			HashString(hash, cache_tag);
			hash.update(&target.format, sizeof(target.format));
			hash.update(&target.profile, sizeof(target.profile));
			hash.update(&desc.optimization, sizeof(desc.optimization));
			hash.update(&desc.enable_debug_info, sizeof(desc.enable_debug_info));
			hash.update(&desc.matrix_layout, sizeof(desc.matrix_layout));

			for (auto const& module : desc.modules) {
				HashString(hash, module.name);
				HashString(hash, module.source);
			}
			for (auto const& entry : desc.entry_points) {
				HashString(hash, entry.name);
				hash.update(&entry.stage, sizeof(entry.stage));
			}
			for (auto const& macro : desc.macros) {
				HashString(hash, macro.name);
				HashString(hash, macro.value);
			}
			for (auto const& path : desc.include_paths) {
				std::error_code error;
				auto normalized = fs::weakly_canonical(path, error);
				HashString(hash, (error ? path.lexically_normal() : normalized).generic_string());
			}
			return std::format("slang-program-{:016x}", hash.result());
		}

		static std::uint64_t SessionHash(slang::TargetDesc const& target, SlangPipelineProgramDescriptor const& desc) {
			boost::hash2::xxhash_64 hash;
			hash.update(&target.format, sizeof(target.format));
			hash.update(&target.profile, sizeof(target.profile));
			hash.update(&desc.optimization, sizeof(desc.optimization));
			hash.update(&desc.enable_debug_info, sizeof(desc.enable_debug_info));
			hash.update(&desc.matrix_layout, sizeof(desc.matrix_layout));
			for (auto const& path : desc.include_paths) {
				std::error_code error;
				auto normalized = fs::weakly_canonical(path, error);
				HashString(hash, (error ? path.lexically_normal() : normalized).generic_string());
			}
			for (auto const& macro : desc.macros) {
				HashString(hash, macro.name);
				HashString(hash, macro.value);
			}
			return hash.result();
		}

		static Slang::ComPtr<slang::ISession> RequestSession(
			slang::TargetDesc const& target,
			SlangPipelineProgramDescriptor const& desc
		) {
			static std::unordered_map<std::uint64_t, Slang::ComPtr<slang::ISession>> session_cache;
			static std::shared_mutex mutex;
			auto key = SessionHash(target, desc);

			{
				std::shared_lock lock(mutex);
				if (auto it = session_cache.find(key); it != session_cache.end()) {
					return it->second;
				}
			}

			std::vector<std::string> path_storage;
			path_storage.reserve(desc.include_paths.size());
			for (auto const& path : desc.include_paths) {
				path_storage.push_back(path.generic_string());
			}
			std::vector<char const*> search_paths;
			search_paths.reserve(path_storage.size());
			for (auto const& path : path_storage) {
				search_paths.push_back(path.c_str());
			}

			std::vector<slang::PreprocessorMacroDesc> macros;
			macros.reserve(desc.macros.size());
			for (auto const& macro : desc.macros) {
				macros.push_back({ macro.name.c_str(), macro.value.c_str() });
			}

			std::array compiler_options = {
				slang::CompilerOptionEntry{
					slang::CompilerOptionName::Optimization,
					{ slang::CompilerOptionValueKind::Int, static_cast<int>(desc.optimization) }
				},
				slang::CompilerOptionEntry{
					slang::CompilerOptionName::DebugInformation,
					{
						slang::CompilerOptionValueKind::Int,
						static_cast<std::int32_t>(
							desc.enable_debug_info ? SLANG_DEBUG_INFO_LEVEL_MAXIMAL : SLANG_DEBUG_INFO_LEVEL_NONE
						)
					}
				}
			};

			slang::SessionDesc session_desc{};
			session_desc.targets = &target;
			session_desc.targetCount = 1;
			session_desc.searchPaths = search_paths.data();
			session_desc.searchPathCount = static_cast<SlangInt>(search_paths.size());
			session_desc.preprocessorMacros = macros.data();
			session_desc.preprocessorMacroCount = static_cast<SlangInt>(macros.size());
			session_desc.compilerOptionEntries = compiler_options.data();
			session_desc.compilerOptionEntryCount = static_cast<std::uint32_t>(compiler_options.size());
			session_desc.defaultMatrixLayoutMode =
				desc.matrix_layout == SlangPipelineProgramDescriptor::MatrixLayout::RowMajor
				? SLANG_MATRIX_LAYOUT_ROW_MAJOR
				: SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

			Slang::ComPtr<slang::ISession> created;
			auto result = SlangGlobalSession()->createSession(session_desc, created.writeRef());
			Check(result, nullptr, "Creating Slang session");

			std::unique_lock lock(mutex);
			auto [it, inserted] = session_cache.try_emplace(key, created);
			return it->second;
		}

		static bool ReadBinary(fs::path const& path, std::vector<std::byte>& output) {
			std::ifstream input(path, std::ios::binary | std::ios::ate);
			if (!input) {
				return false;
			}
			auto size = input.tellg();
			if (size < 0) {
				return false;
			}
			output.resize(static_cast<std::size_t>(size));
			input.seekg(0, std::ios::beg);
			if (!output.empty()) {
				input.read(reinterpret_cast<char*>(output.data()), size);
			}
			return input.good() || input.eof();
		}

		static bool ValidateDependencies(nlohmann::json const& manifest) {
			if (!manifest.contains("dependencies") || !manifest["dependencies"].is_array()) {
				return false;
			}
			for (auto const& dependency : manifest["dependencies"]) {
				auto path = fs::path(dependency.at("path").get<std::string>());
				std::error_code error;
				if (!fs::is_regular_file(path, error) || error) {
					return false;
				}
				if (HashFile(path) != dependency.at("hash").get<std::uint64_t>()) {
					return false;
				}
			}
			return true;
		}

		static nlohmann::json SerializeInterface(SlangPipelineInterface const& interface) {
			nlohmann::json result{
				{ "bindings", nlohmann::json::array() },
				{ "vertex_inputs", nlohmann::json::array() },
				{ "fragment_outputs", nlohmann::json::array() },
				{ "push_constants", nlohmann::json::array() }
			};
			for (auto const& entry : interface.bindings) {
				nlohmann::json flags = nlohmann::json::array();
				auto words = entry.flags.Snapshot();
				for (auto word : words) {
					flags.push_back(word);
				}
				result["bindings"].push_back(
					{
						{ "name", entry.name },
						{ "flags", std::move(flags) },
						{ "binding", entry.slot },
						{ "space", entry.space },
						{ "count", entry.count },
						{ "visibility", entry.visibility }
					}
				);
			}
			for (auto const& input : interface.vertex_inputs) {
				result["vertex_inputs"].push_back(
					{
						{ "name", input.name },
						{ "location", input.location },
						{ "semantic_name", input.semantic_name },
						{ "semantic_index", input.semantic_index }
					}
				);
			}
			for (auto const& output : interface.fragment_outputs) {
				result["fragment_outputs"].push_back(
					{ { "name", output.name }, { "location", output.location } }
				);
			}
			for (auto const& range : interface.push_constants) {
				result["push_constants"].push_back(
					{
						{ "offset", range.offset },
						{ "size", range.size },
						{ "binding", range.slot },
						{ "space", range.space },
						{ "visibility", range.visibility }
					}
				);
			}
			return result;
		}

		static SlangPipelineInterface DeserializeInterface(nlohmann::json const& json) {
			SlangPipelineInterface result;
			for (auto const& item : json.at("bindings")) {
				SlangPipelineBinding entry;
				entry.name = item.at("name").get<std::string>();
				auto words = entry.flags.Snapshot();
				auto const& serialized_words = item.at("flags");
				if (serialized_words.size() != words.size()) {
					throw std::runtime_error("Pipeline interface flag word count mismatch");
				}
				for (std::size_t index = 0; index < words.size(); ++index) {
					words[index] = serialized_words[index].get<std::size_t>();
				}
				entry.flags.Assign(words);
				entry.slot = item.at("binding").get<std::uint32_t>();
				entry.space = item.at("space").get<std::uint32_t>();
				entry.count = item.at("count").get<std::uint32_t>();
				entry.visibility = item.at("visibility").get<std::uint32_t>();
				result.bindings.push_back(std::move(entry));
			}
			for (auto const& item : json.at("vertex_inputs")) {
				result.vertex_inputs.push_back(
					{
						item.at("name").get<std::string>(),
						item.at("location").get<std::uint32_t>(),
						item.at("semantic_name").get<std::string>(),
						item.at("semantic_index").get<std::uint32_t>()
					}
				);
			}
			for (auto const& item : json.at("fragment_outputs")) {
				result.fragment_outputs.push_back(
					{
						item.at("name").get<std::string>(),
						item.at("location").get<std::uint32_t>()
					}
				);
			}
			for (auto const& item : json.at("push_constants")) {
				result.push_constants.push_back(
					{
						item.at("offset").get<std::uint32_t>(),
						item.at("size").get<std::uint32_t>(),
						item.at("binding").get<std::uint32_t>(),
						item.at("space").get<std::uint32_t>(),
						item.at("visibility").get<std::uint32_t>()
					}
				);
			}
			return result;
		}

		static std::uint32_t StageBit(PipelineStage stage) {
			return 1u << static_cast<std::uint32_t>(stage);
		}

		static std::uint32_t StageVisibility(SlangPipelineProgramDescriptor const& desc) {
			std::uint32_t result = 0;
			for (auto const& entry : desc.entry_points) {
				result |= StageBit(entry.stage);
			}
			return result;
		}

		static std::uint32_t CheckedUint32(std::size_t value, std::string_view subject) {
			if (
				value == SLANG_UNKNOWN_SIZE ||
				value == SLANG_UNBOUNDED_SIZE ||
				value > std::numeric_limits<std::uint32_t>::max()
			) {
				throw std::runtime_error(std::format("Slang reported an unsupported {} value ({})", subject, value));
			}
			return static_cast<std::uint32_t>(value);
		}

		static std::uint32_t BindingCount(slang::TypeLayoutReflection* type) {
			if (!type || !type->isArray()) {
				return 1;
			}
			return CheckedUint32(type->getTotalArrayElementCount(), "resource array size");
		}

		static ResourceFlags BindingFlags(
			slang::ParameterCategory category,
			slang::TypeLayoutReflection* type
		) {
			ResourceFlags flags;
			auto leaf = type ? type->unwrapArray() : nullptr;
			auto binding_type =
				leaf && leaf->getBindingRangeCount() != 0
					? leaf->getBindingRangeType(0)
					: slang::BindingType::Unknown;
			switch (binding_type) {
			case slang::BindingType::Sampler:
				flags.Set(ResourceFlagBits::SamplerBinding);
				return flags;
			case slang::BindingType::ConstantBuffer:
			case slang::BindingType::ParameterBlock:
			case slang::BindingType::InlineUniformData:
				flags.Set(ResourceFlagBits::UniformBuffer);
				return flags;
			case slang::BindingType::Texture:
			case slang::BindingType::InputRenderTarget:
				flags.Set(ResourceFlagBits::TextureBinding);
				return flags;
			case slang::BindingType::MutableTexture:
				flags.Set(ResourceFlagBits::StorageBinding);
				return flags;
			case slang::BindingType::TypedBuffer:
			case slang::BindingType::RawBuffer:
				flags.Set(ResourceFlagBits::StorageBuffer);
				flags.Set(ResourceFlagBits::TextureBinding);
				return flags;
			case slang::BindingType::MutableTypedBuffer:
			case slang::BindingType::MutableRawBuffer:
				flags.Set(ResourceFlagBits::StorageBuffer);
				flags.Set(ResourceFlagBits::StorageBinding);
				return flags;
			case slang::BindingType::CombinedTextureSampler:
				flags.Set(ResourceFlagBits::TextureBinding);
				flags.Set(ResourceFlagBits::SamplerBinding);
				return flags;
			default:
				break;
			}

			// Some targets expose only the register category at this level.
			switch (category) {
			case slang::ParameterCategory::ConstantBuffer:
				flags.Set(ResourceFlagBits::UniformBuffer);
				break;
			case slang::ParameterCategory::SamplerState:
				flags.Set(ResourceFlagBits::SamplerBinding);
				break;
			case slang::ParameterCategory::ShaderResource:
			case slang::ParameterCategory::UnorderedAccess: {
				auto shape = leaf
					? leaf->getResourceShape() & SLANG_RESOURCE_BASE_SHAPE_MASK
					: SLANG_RESOURCE_NONE;
				if (shape == SLANG_STRUCTURED_BUFFER || shape == SLANG_BYTE_ADDRESS_BUFFER) {
					flags.Set(ResourceFlagBits::StorageBuffer);
				}
				flags.Set(
					category == slang::ParameterCategory::ShaderResource
						? ResourceFlagBits::TextureBinding
						: ResourceFlagBits::StorageBinding
				);
				break;
			}
			default:
				break;
			}
			return flags;
		}

		static void ReflectVarying(
			slang::VariableLayoutReflection* variable,
			bool input,
			SlangPipelineInterface& interface
		) {
			if (!variable || !variable->getTypeLayout()) {
				return;
			}
			auto type = variable->getTypeLayout();
			if (type->getFieldCount() != 0) {
				for (unsigned index = 0; index < type->getFieldCount(); ++index) {
					ReflectVarying(type->getFieldByIndex(index), input, interface);
				}
				return;
			}

			auto name = variable->getName() ? variable->getName() : "";
			auto semantic = variable->getSemanticName() ? variable->getSemanticName() : "";
			auto location = CheckedUint32(variable->getBindingIndex(), "varying location");
			if (input) {
				interface.vertex_inputs.push_back(
					{
						name,
						location,
						semantic,
						CheckedUint32(variable->getSemanticIndex(), "semantic index")
					}
				);
			}
			else {
				interface.fragment_outputs.push_back({ name, location });
			}
		}

		static SlangPipelineInterface ReflectInterface(
			slang::ProgramLayout* layout,
			SlangPipelineProgramDescriptor const& desc
		) {
			SlangPipelineInterface result;
			auto visibility = StageVisibility(desc);

			for (unsigned index = 0; index < layout->getParameterCount(); ++index) {
				auto variable = layout->getParameterByIndex(index);
				if (!variable || !variable->getTypeLayout()) {
					continue;
				}
				auto category = variable->getCategory();
				if (category == slang::ParameterCategory::PushConstantBuffer) {
					result.push_constants.push_back(
						{
							CheckedUint32(variable->getOffset(category), "push constant offset"),
							CheckedUint32(variable->getTypeLayout()->getSize(slang::ParameterCategory::Uniform), "push constant size"),
							variable->getBindingIndex(),
							variable->getBindingSpace(),
							visibility
						}
					);
					continue;
				}

				auto flags = BindingFlags(category, variable->getTypeLayout());
				if (flags.None()) {
					continue;
				}
				result.bindings.push_back(
					{
						variable->getName() ? variable->getName() : "",
						std::move(flags),
						variable->getBindingIndex(),
						variable->getBindingSpace(),
						BindingCount(variable->getTypeLayout()),
						visibility
					}
				);
			}

			for (SlangUInt index = 0; index < layout->getEntryPointCount(); ++index) {
				auto entry = layout->getEntryPointByIndex(index);
				if (!entry || index >= desc.entry_points.size()) {
					continue;
				}
				auto stage = desc.entry_points[index].stage;
				if (stage == PipelineStage::Vertex) {
					for (unsigned parameter_index = 0; parameter_index < entry->getParameterCount(); ++parameter_index) {
						auto parameter = entry->getParameterByIndex(parameter_index);
						if (parameter && parameter->getCategory() == slang::ParameterCategory::VaryingInput) {
							ReflectVarying(parameter, true, result);
						}
					}
				}
				else if (stage == PipelineStage::Fragment) {
					ReflectVarying(entry->getResultVarLayout(), false, result);
				}
			}
			return result;
		}

		bool TryLoadCache(fs::path const& directory, std::string_view key) {
			try {
				std::ifstream manifest_file(directory / "manifest.json", std::ios::binary);
				if (!manifest_file) {
					return false;
				}

				nlohmann::json manifest;
				manifest_file >> manifest;
				if (
					manifest.at("schema").get<std::uint32_t>() != CACHE_SCHEMA_VERSION ||
					manifest.at("key").get<std::string>() != key ||
					manifest.at("slang").get<std::string>() != SlangGlobalSession()->getBuildTagString() ||
					!ValidateDependencies(manifest)
				) {
					return false;
				}

				std::vector<SlangCompiledEntryPoint> loaded_entries;
				for (auto const& entry : manifest.at("entry_points")) {
					std::vector<std::byte> code;
					if (!ReadBinary(directory / entry.at("file").get<std::string>(), code)) {
						return false;
					}
					if (
						code.size() != entry.at("size").get<std::size_t>() ||
						HashBytes(code) != entry.at("hash").get<std::uint64_t>()
					) {
						return false;
					}
					loaded_entries.push_back(
						{
							static_cast<PipelineStage>(entry.at("stage").get<std::uint32_t>()),
							entry.at("name").get<std::string>(),
							std::move(code)
						}
					);
				}

				std::ifstream reflection_file(directory / manifest.at("reflection").get<std::string>(), std::ios::binary);
				if (!reflection_file) {
					return false;
				}
				std::string reflection{
					std::istreambuf_iterator<char>(reflection_file),
					std::istreambuf_iterator<char>()
				};
				if (
					reflection.size() != manifest.at("reflection_size").get<std::size_t>() ||
					HashBytes(std::as_bytes(std::span(reflection))) != manifest.at("reflection_hash").get<std::uint64_t>()
				) {
					return false;
				}

				std::ifstream interface_file(directory / manifest.at("interface").get<std::string>(), std::ios::binary);
				if (!interface_file) {
					return false;
				}
				nlohmann::json serialized_interface;
				interface_file >> serialized_interface;

				m_entry_points = std::move(loaded_entries);
				m_interface = DeserializeInterface(serialized_interface);
				m_reflection_json = std::move(reflection);
				return true;
			}
			catch (std::exception const& error) {
				LOG_WARNING(std::format("Ignoring invalid Slang cache '{}': {}", directory.string(), error.what()));
				return false;
			}
		}

		static void WriteFileAtomically(fs::path const& path, std::span<std::byte const> bytes) {
			auto temporary = path;
			temporary += std::format(".tmp-{:x}", std::hash<std::thread::id>{}(std::this_thread::get_id()));
			{
				std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
				if (!output) {
					throw std::runtime_error(std::format("Cannot create cache file '{}'", temporary.string()));
				}
				if (!bytes.empty()) {
					output.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
				}
				output.flush();
				if (!output) {
					throw std::runtime_error(std::format("Failed to write cache file '{}'", temporary.string()));
				}
			}

			std::error_code error;
			fs::remove(path, error);
			error.clear();
			fs::rename(temporary, path, error);
			if (error) {
				fs::remove(temporary);
				throw std::runtime_error(std::format("Cannot publish cache file '{}': {}", path.string(), error.message()));
			}
		}

		static void WriteTextAtomically(fs::path const& path, std::string_view text) {
			WriteFileAtomically(path, std::as_bytes(std::span(text)));
		}

		void WriteCache(
			fs::path const& directory,
			std::string_view key,
			std::span<Slang::ComPtr<slang::IModule> const> modules
		) const {
			try {
				nlohmann::json manifest{
					{ "schema", CACHE_SCHEMA_VERSION },
					{ "key", key },
					{ "slang", SlangGlobalSession()->getBuildTagString() },
					{ "interface", "interface.json" },
					{ "reflection", "reflection.json" },
					{ "reflection_size", m_reflection_json.size() },
					{ "reflection_hash", HashBytes(std::as_bytes(std::span(m_reflection_json))) },
					{ "entry_points", nlohmann::json::array() },
					{ "dependencies", nlohmann::json::array() }
				};

				for (std::size_t index = 0; index < m_entry_points.size(); ++index) {
					auto const& entry = m_entry_points[index];
					auto filename = std::format("entry-{}.bin", index);
					WriteFileAtomically(directory / filename, entry.code);
					manifest["entry_points"].push_back(
						{
							{ "name", entry.name },
							{ "stage", static_cast<std::uint32_t>(entry.stage) },
							{ "file", filename },
							{ "size", entry.code.size() },
							{ "hash", HashBytes(entry.code) }
						}
					);
				}

				std::unordered_map<std::string, std::uint64_t> dependencies;
				for (auto const& module : modules) {
					for (SlangInt32 index = 0; index < module->getDependencyFileCount(); ++index) {
						auto dependency_path = module->getDependencyFilePath(index);
						if (!dependency_path || !*dependency_path) {
							continue;
						}
						std::error_code error;
						auto path = fs::weakly_canonical(dependency_path, error);
						if (error || !fs::is_regular_file(path, error) || error) {
							continue;
						}
						dependencies.try_emplace(path.generic_string(), HashFile(path));
					}
				}
				for (auto const& [path, hash] : dependencies) {
					manifest["dependencies"].push_back({ { "path", path }, { "hash", hash } });
				}

				WriteTextAtomically(directory / "interface.json", SerializeInterface(m_interface).dump(2));
				WriteTextAtomically(directory / "reflection.json", m_reflection_json);
				// Publish the manifest last. Its presence marks a complete cache entry.
				WriteTextAtomically(directory / "manifest.json", manifest.dump(2));
			}
			catch (std::exception const& error) {
				LOG_WARNING(std::format("Failed to write Slang cache '{}': {}", directory.string(), error.what()));
			}
		}

	public:
		SlangProgram(
			slang::TargetDesc const& target,
			SlangPipelineProgramDescriptor const& desc,
			std::string_view cache_tag
		) {
			if (desc.modules.empty()) {
				throw std::invalid_argument("Slang program has no modules");
			}
			if (desc.entry_points.empty()) {
				throw std::invalid_argument("Slang program has no entry points");
			}

			auto cache_key = BuildCacheKey(target, desc, cache_tag);
			auto cache_directory = cache::GetCacheDirectory(cache_key);
			static std::mutex cache_mutex;
			std::unique_lock cache_lock(cache_mutex);
			if (TryLoadCache(cache_directory, cache_key)) {
				return;
			}

			auto session = RequestSession(target, desc);
			Slang::ComPtr<slang::IBlob> diagnostics;
			std::vector<Slang::ComPtr<slang::IModule>> modules;
			std::vector<Slang::ComPtr<slang::IEntryPoint>> entries;
			std::vector<slang::IComponentType*> components;

			for (auto const& module_desc : desc.modules) {
				if (module_desc.name.empty()) {
					throw std::invalid_argument("Slang module has no name");
				}
				auto path = module_desc.name + ".slang";
				Slang::ComPtr<slang::IModule> module(
					session->loadModuleFromSourceString(
						module_desc.name.c_str(),
						path.c_str(),
						module_desc.source.c_str(),
						diagnostics.writeRef()
					)
				);
				if (!module) {
					Check(SLANG_FAIL, diagnostics, "Loading Slang module");
				}
				Check(SLANG_OK, diagnostics, "Loading Slang module");
				components.push_back(module);
				modules.push_back(std::move(module));
			}

			for (auto const& entry_desc : desc.entry_points) {
				Slang::ComPtr<slang::IEntryPoint> entry;
				SlangResult result = SLANG_E_NOT_FOUND;
				for (auto const& module : modules) {
					result = module->findEntryPointByName(entry_desc.name.c_str(), entry.writeRef());
					if (SLANG_SUCCEEDED(result) && entry) {
						break;
					}
				}
				Check(result, diagnostics, std::format("Finding entry point '{}'", entry_desc.name));
				components.push_back(entry);
				entries.push_back(std::move(entry));
			}

			Slang::ComPtr<slang::IComponentType> composed;
			auto result = session->createCompositeComponentType(
				components.data(),
				static_cast<SlangInt>(components.size()),
				composed.writeRef(),
				diagnostics.writeRef()
			);
			Check(result, diagnostics, "Composing Slang program");

			Slang::ComPtr<slang::IComponentType> linked_program;
			result = composed->link(linked_program.writeRef(), diagnostics.writeRef());
			Check(result, diagnostics, "Linking Slang program");

			m_entry_points.reserve(desc.entry_points.size());
			for (std::size_t index = 0; index < desc.entry_points.size(); ++index) {
				Slang::ComPtr<slang::IBlob> code;
				result = linked_program->getEntryPointCode(
					static_cast<SlangInt>(index),
					0,
					code.writeRef(),
					diagnostics.writeRef()
				);
				Check(result, diagnostics, std::format("Generating '{}'", desc.entry_points[index].name));
				auto begin = static_cast<std::byte const*>(code->getBufferPointer());
				m_entry_points.push_back(
					{
						desc.entry_points[index].stage,
						desc.entry_points[index].name,
						{ begin, begin + code->getBufferSize() }
					}
				);
			}

			auto reflection = linked_program->getLayout(0, diagnostics.writeRef());
			if (!reflection) {
				Check(SLANG_FAIL, diagnostics, "Reflecting Slang program");
			}
			m_interface = ReflectInterface(reflection, desc);
			Slang::ComPtr<ISlangBlob> reflection_blob;
			result = reflection->toJson(reflection_blob.writeRef());
			Check(result, nullptr, "Serializing Slang reflection");
			m_reflection_json.assign(
				static_cast<char const*>(reflection_blob->getBufferPointer()),
				reflection_blob->getBufferSize()
			);

			WriteCache(cache_directory, cache_key, modules);
		}

		std::span<SlangCompiledEntryPoint const> EntryPoints() const noexcept {
			return m_entry_points;
		}

		SlangPipelineInterface const& Interface() const noexcept {
			return m_interface;
		}

		std::string_view ReflectionJSON() const noexcept {
			return m_reflection_json;
		}
	};

}
