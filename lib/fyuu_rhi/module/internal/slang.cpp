module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>
#include <functional>
#include <iterator>
#include <string>
#include <limits>
#include <fstream>

#include <cstdint>
#include <array>
#include <unordered_map>
#include <mutex>
#include <thread>

#include <shared_mutex>

#include <ranges>
#include <string_view>
#include <filesystem>

#include <source_location>
#include <span>
#include <format>
#endif

#include <slang.h>
#include <slang-com-ptr.h>
#include <boost/hash2/xxhash.hpp>
#include <nlohmann/json.hpp>

module fyuu_rhi:slang;
#if defined(__cpp_lib_modules)
import std;
#endif
import :pipeline;
import :log;
import :cache;

namespace fs = std::filesystem;

namespace fyuu_rhi::pipeline {

	struct SlangPipelineBinding {
		std::string name;
		ResourceFlags flags;
		std::uint32_t slot = 0;
		std::uint32_t space = 0;
		std::uint32_t count = 1;
		std::uint32_t visibility = 0;
	};

	struct SlangPipelineVertexInput {
		std::string name;
		std::uint32_t location = 0;
		std::string semantic_name;
		std::uint32_t semantic_index = 0;
	};

	struct SlangPipelineFragmentOutput {
		std::string name;
		std::uint32_t location = 0;
	};

	struct SlangPipelinePushConstantRange {
		std::uint32_t offset = 0;
		std::uint32_t size = 0;
		std::uint32_t slot = 0;
		std::uint32_t space = 0;
		std::uint32_t visibility = 0;
	};

	struct SlangPipelineInterface {
		std::vector<SlangPipelineBinding> bindings;
		std::vector<SlangPipelineVertexInput> vertex_inputs;
		std::vector<SlangPipelineFragmentOutput> fragment_outputs;
		std::vector<SlangPipelinePushConstantRange> push_constants;
	};

	/// Drops the reflection-only name/visibility so backends can iterate the
	/// layout without the shader module.
	std::vector<BindingMetadata> MakePipelineBindingMetadata(SlangPipelineInterface const& interface) {
		std::vector<BindingMetadata> result;
		result.reserve(interface.bindings.size());
		std::ranges::transform(
			interface.bindings,
			std::back_inserter(result),
			[&interface](auto const& entry) {
				bool combined =
					entry.flags.Test(ResourceFlagBits::TextureBinding) &&
					entry.flags.Test(ResourceFlagBits::SamplerBinding);
				if (combined && entry.count != 1u) {
					throw std::invalid_argument(
						"Combined texture/sampler binding arrays are not supported"
					);
				}
				if (
					combined &&
					std::ranges::any_of(
						interface.bindings,
						[&entry](auto const& candidate) {
							return
								&candidate != &entry &&
								candidate.space == entry.space &&
								candidate.slot == entry.slot + 1u;
						}
					)
				) {
					throw std::invalid_argument(
						"A combined texture/sampler binding reserves its following slot"
					);
				}
				return BindingMetadata{
					.flags = entry.flags,
					.slot = entry.slot,
					.space = entry.space,
					.count = entry.count
				};
			}
		);
		return result;
	}

}

namespace fyuu_rhi::shader {

	using namespace fyuu_rhi::pipeline;

	Slang::ComPtr<slang::IGlobalSession> SlangGlobalSession() {
		static std::once_flag once;
		static Slang::ComPtr<slang::IGlobalSession> session;
		static SlangResult creation_result = SLANG_FAIL;
		std::call_once(
			once, 
			[&](){
				creation_result = slang::createGlobalSession(session.writeRef());
			}
		);
		if (SLANG_FAILED(creation_result) || !session) {
			throw std::runtime_error(std::format("Failed to create Slang global session ({})", creation_result));
		}
		return session;
	}

	struct SlangCompiledEntryPoint {
		Stage stage;
		std::string name;
		std::vector<std::byte> code;
	};

	/// Compiles a set of Slang modules and entry points to a backend bytecode
	/// (DXIL / SPIR-V / WGSL / MSL) and reflects the pipeline interface out of
	/// the program: resource bindings, vertex inputs, fragment outputs and push
	/// constant ranges.
	///
	/// Compilation is expensive, so every result is persisted to the disk cache
	/// (`cache::GetCacheDirectory`) under a key derived from the source, target
	/// and cache tag. On a later construction with the same key the compiled
	/// entry points, the reflected interface and the raw reflection JSON are
	/// loaded back without invoking the compiler. Construction is additionally
	/// serialized behind a process-wide mutex so concurrent pipeline creation
	/// cannot race the cache read/write.
	///
	/// Consumers use the result through GetEntryPoints() (per-stage bytecode),
	/// GetInterface() (the reflection backends build their binding layout from)
	/// and GetReflectionJSON() (the raw Slang reflection dump, for tooling).
	class SlangProgram {
	private:
		/// Bumped whenever the on-disk cache format changes; older entries are
		/// ignored instead of being parsed with a mismatched schema.
		static constexpr std::uint32_t CACHE_SCHEMA_VERSION = 4u;

		/// Per-entry-point compiled bytecode, in program order.
		std::vector<SlangCompiledEntryPoint> m_entry_points;
		/// The reflected pipeline interface (bindings / varyings / push constants).
		SlangPipelineInterface m_interface;
		/// The raw Slang reflection dump (JSON), preserved verbatim for tooling.
		std::string m_reflection_json;

		// Throws if a Slang call failed, and logs the compiler diagnostics blob
		// as a warning when present (Slang often reports recoverable notices).
		static void Check(SlangResult result, slang::IBlob* diagnostics, std::string_view operation) {
			std::string_view message;
			if (diagnostics && diagnostics->getBufferPointer() && diagnostics->getBufferSize() != 0) {
				message = {
					static_cast<char const*>(diagnostics->getBufferPointer()),
					diagnostics->getBufferSize()
				};
			}
			if (SLANG_FAILED(result)) {
				throw std::runtime_error(std::format("{} failed ({}): {}", operation, result, message));
			}
			if (!message.empty()) {
				log::Warning(std::format("{}: {}", operation, message));
			}
		}

		// Length-prefixed hash of a string so "ab"+"c" and "a"+"bc" differ.
		static void HashString(boost::hash2::xxhash_64& hash, std::string_view value) {
			auto size = static_cast<std::uint64_t>(value.size());
			hash.update(&size, sizeof(size));
			hash.update(value.data(), value.size());
		}

		static std::uint64_t HashBytes(std::span<std::byte const> bytes) {
			boost::hash2::xxhash_64 hash;
			hash.update(bytes.data(), bytes.size());
			return hash.result();
		}

		// Stream-hashes a whole file; used to fingerprint included .slang
		// dependencies so the cache invalidates when they change on disk.
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

		// Derives the disk-cache key from everything that can change the compiled
		// output: schema version, Slang build tag, the caller's cache tag (usually
		// the driver identity, e.g. d3d12 vendor/device/driver), the target, the
		// optimization/debug/matrix-layout settings and the module source itself.
		static std::string BuildCacheKey(slang::TargetDesc const& target, SlangPipelineProgramDescriptor const& desc, std::string_view cache_tag) {
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

		// Slang sessions are heavy, so they are cached in memory by the settings
		// that affect them (target + optimization + include paths + macros).
		// Multiple compilation units with the same settings share one session.
		static Slang::ComPtr<slang::ISession> RequestSession(slang::TargetDesc const& target, SlangPipelineProgramDescriptor const& desc) {
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

		// Reads a whole binary file; returns false instead of throwing when the
		// file is missing or unreadable (used while probing the disk cache).
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

		// A cached entry lists the included .slang files it was built from. Before
		// trusting the cache each dependency must still exist and hash to the same
		// value; any change invalidates the whole entry.
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

		// JSON round-trip of the reflected interface so it can be persisted next
		// to the compiled bytecode and restored without re-running reflection.
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

		// Inverse of SerializeInterface; restores the interface from cache JSON.
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

		// Builds a bitmask of which pipeline stages the bindings are visible to,
		// derived from the requested entry points. Stored per binding so backends
		// can set per-stage shader visibility (e.g. D3D12 root parameters).
		static std::uint32_t StageVisibility(SlangPipelineProgramDescriptor const& desc) {
			std::uint32_t result = 0;
			for (auto const& entry : desc.entry_points) {
				result |= 1u << static_cast<std::uint32_t>(entry.stage);
			}
			return result;
		}

		// Slang reports unknown/unbounded sizes as sentinel values; reject those
		// and anything that would not fit a u32 instead of truncating silently.
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

		// How many descriptors a binding spans: 1 for a scalar, or the array
		// element count for a resource array.
		static std::uint32_t BindingCount(slang::TypeLayoutReflection* type) {
			if (!type || !type->isArray()) {
				return 1;
			}
			return CheckedUint32(type->getTotalArrayElementCount(), "resource array size");
		}

		// Maps a Slang binding (sampler, constant buffer, texture, storage, ...)
		// onto the engine's ResourceFlags so backends can pick the right
		// descriptor type (CBV/SRV/UAV/sampler, texture vs buffer, ...).
		static ResourceFlags BindingFlags(slang::ParameterCategory category, slang::TypeLayoutReflection* type) {
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

		// Reflects a varying (vertex input or fragment output): recursively
		// descends struct fields and records the semantic name/index + location
		// needed by backends to build the vertex input layout / render targets.
		static void ReflectVarying(slang::VariableLayoutReflection* variable, bool input, SlangPipelineInterface& interface) {
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

		// Walks the linked program layout and turns it into the engine's
		// SlangPipelineInterface: resource parameters (bindings with slot/space/
		// count/flags/visibility), push-constant ranges, and per-stage varyings.
		static SlangPipelineInterface ReflectInterface(slang::ProgramLayout* layout, SlangPipelineProgramDescriptor const& desc) {
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
				if (stage == Stage::Vertex) {
					for (unsigned parameter_index = 0; parameter_index < entry->getParameterCount(); ++parameter_index) {
						auto parameter = entry->getParameterByIndex(parameter_index);
						if (parameter && parameter->getCategory() == slang::ParameterCategory::VaryingInput) {
							ReflectVarying(parameter, true, result);
						}
					}
				}
				else if (stage == Stage::Fragment) {
					ReflectVarying(entry->getResultVarLayout(), false, result);
				}
			}
			return result;
		}

		// Attempts to restore a previously compiled program from the cache
		// directory. The manifest must match the schema/key/Slang build tag and
		// every dependency must still hash identically; entry-point bytecode is
		// re-verified by size + hash before it is accepted. Any inconsistency
		// falls back to a fresh compile.
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
							static_cast<Stage>(entry.at("stage").get<std::uint32_t>()),
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
				log::Warning(
					std::format(
						"Ignoring invalid Slang cache '{}': {}",
						directory.string(),
						error.what()
					)
				);
				return false;
			}
		}

		// Writes to a per-thread temporary file then renames it into place, so a
		// concurrent reader never observes a partially written cache entry.
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

		// Persists the compiled result: one .bin per entry point, interface.json,
		// reflection.json, the dependency file hashes, and finally manifest.json
		// (published last so its presence marks a complete, usable entry).
		void WriteCache(fs::path const& directory, std::string_view key, std::span<Slang::ComPtr<slang::IModule> const> modules) const {
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

				auto serialized_interface = SerializeInterface(m_interface).dump(2);
				WriteFileAtomically(
					directory / "interface.json",
					std::as_bytes(std::span(serialized_interface))
				);
				WriteFileAtomically(
					directory / "reflection.json",
					std::as_bytes(std::span(m_reflection_json))
				);
				// Publish the manifest last. Its presence marks a complete cache entry.
				auto serialized_manifest = manifest.dump(2);
				WriteFileAtomically(
					directory / "manifest.json",
					std::as_bytes(std::span(serialized_manifest))
				);
			}
			catch (std::exception const& error) {
				log::Warning(
					std::format(
						"Failed to write Slang cache '{}': {}",
						directory.string(),
						error.what()
					)
				);
			}
		}

	public:
		SlangProgram(slang::TargetDesc const& target, SlangPipelineProgramDescriptor const& desc, std::string_view cache_tag) {
			if (desc.modules.empty()) {
				throw std::invalid_argument("Slang program has no modules");
			}
			if (desc.entry_points.empty()) {
				throw std::invalid_argument("Slang program has no entry points");
			}

			// Fast path: a valid cache entry for this key skips the compiler
			// entirely. The lock serializes construction so concurrent pipeline
			// creation cannot read while another thread is writing the cache.
			auto cache_key = BuildCacheKey(target, desc, cache_tag);
			auto cache_directory = cache::GetCacheDirectory(cache_key);
			static std::mutex cache_mutex;
			std::unique_lock cache_lock(cache_mutex);
			if (TryLoadCache(cache_directory, cache_key)) {
				return;
			}

			// Compile path. Grab (or reuse) a Slang session configured for this
			// target and load each module from its source string.
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

			// Locate each requested entry point by name in the loaded modules.
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

			// Combine modules + entry points into one component type and link it.
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

			// Emit backend bytecode (DXIL / SPIR-V / WGSL / MSL) per entry point.
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

			// Reflect the linked program: the engine-facing interface plus the
			// raw JSON dump preserved for tooling/debugging.
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

			// Persist the compiled result so a later construction with the same
			// key returns from the disk cache instead of recompiling.
			WriteCache(cache_directory, cache_key, modules);
		}

		/// Compiled backend bytecode, one element per entry point, in program order.
		std::span<SlangCompiledEntryPoint const> GetEntryPoints() const noexcept {
			return m_entry_points;
		}

		/// Reflected pipeline interface (bindings / varyings / push constants);
		/// backends build their root signatures / layouts from this.
		SlangPipelineInterface const& GetInterface() const noexcept {
			return m_interface;
		}

		/// Raw Slang reflection dump (JSON), preserved for tooling/debugging.
		std::string_view GetReflectionJSON() const noexcept {
			return m_reflection_json;
		}
	};

}
