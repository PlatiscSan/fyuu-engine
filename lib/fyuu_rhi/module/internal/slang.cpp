module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <exception>
#include <stdexcept>
#include <utility>

#include <vector>

#include <algorithm>
#include <iterator>

#include <string>

#include <limits>

#include <fstream>

#include <cstdint>

#include <array>
#include <unordered_map>

#include <mutex>

#include <string_view>

#include <filesystem>

#include <ranges>
#include <span>

#include <format>
#endif

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
/** @brief Keep the Windows COM spelling macro from rewriting Slang reflection identifiers. */
#if defined(interface)
#undef interface
#endif // defined(interface)
#endif // defined(_WIN32)

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

namespace fyuu_rhi::pipeline {

	/** @brief Reflected logical binding and target-specific resource/sampler registers. */
	struct SlangPipelineBinding {
		/// @brief Source declaration name.
		std::string name;
		/// @brief Resource shape and shader access properties.
		ResourceFlags flags;

		/// @brief Backend-neutral binding slot.
		std::uint32_t slot = 0;
		/// @brief Backend-neutral resource space.
		std::uint32_t space = 0;

		/// @brief Resource register in the target shader.
		std::uint32_t resource_slot = 0;
		/// @brief Resource register space in the target shader.
		std::uint32_t resource_space = 0;

		/// @brief Sampler register in the target shader.
		std::uint32_t sampler_slot = 0;
		/// @brief Sampler register space in the target shader.
		std::uint32_t sampler_space = 0;
		/// @brief Declared descriptor array length.
		std::uint32_t count = 1;
		/// @brief Bit mask of shader stages that use this declaration.
		std::uint32_t visibility = 0;
	};

	/** @brief Vertex attribute identity and semantic expected by the pipeline. */
	struct SlangPipelineVertexInput {
		/// @brief Source declaration name.
		std::string name;
		/// @brief Shader varying location.
		std::uint32_t location = 0;
		/// @brief Native input semantic name.
		std::string semantic_name;
		/// @brief Native input semantic index.
		std::uint32_t semantic_index = 0;
	};

	/** @brief Fragment output location and source name. */
	struct SlangPipelineFragmentOutput {
		/// @brief Source declaration name.
		std::string name;
		/// @brief Shader varying location.
		std::uint32_t location = 0;
	};

	/** @brief Pipeline-constant ABI range and target-specific register. */
	struct SlangPipelinePushConstantRange {
		/// @brief First byte of the pipeline-constant range.
		std::uint32_t offset = 0;
		/// @brief Number of bytes in the pipeline-constant range.
		std::uint32_t size = 0;

		/// @brief Backend-neutral pipeline-constant slot.
		std::uint32_t abi_slot = 0;
		/// @brief Backend-neutral pipeline-constant space.
		std::uint32_t abi_space = 0;

		/// @brief Register used by the native pipeline-constant binding.
		std::uint32_t register_slot = 0;
		/// @brief Native pipeline-constant register space.
		std::uint32_t register_space = 0;
		/// @brief Bit mask of shader stages that use this declaration.
		std::uint32_t visibility = 0;
	};

	/** @brief Owned binding, varying and pipeline-constant reflection. */
	struct SlangPipelineInterface {
		/// @brief Owned resource binding reflection.
		std::vector<SlangPipelineBinding> bindings;
		/// @brief Owned vertex input reflection.
		std::vector<SlangPipelineVertexInput> vertex_inputs;
		/// @brief Owned fragment output reflection.
		std::vector<SlangPipelineFragmentOutput> fragment_outputs;
		/// @brief Owned pipeline-constant ranges.
		std::vector<SlangPipelinePushConstantRange> push_constants;
	};

	/** @brief Produces backend binding metadata after validating combined descriptor arrays. */
	std::vector<BindingMetadata> MakePipelineBindingMetadata(
	    SlangPipelineInterface const& interface
	) {
		std::vector<BindingMetadata> result;
		result.reserve(interface.bindings.size());
		std::ranges::transform(
		    interface.bindings,
		    std::back_inserter(result),
		    [](auto const& entry) {
			    bool combined = entry.flags.Test(ResourceFlagBits::TextureBinding) &&
			        entry.flags.Test(ResourceFlagBits::SamplerBinding);
			    if (combined && entry.count != 1u) {
				    throw std::invalid_argument(
				        "Combined texture/sampler binding arrays are not supported"
				    );
			    }
			    return BindingMetadata{
			        .flags = entry.flags,
			        .slot = entry.slot,
			        .space = entry.space,
			        .count = entry.count,
			        .sampler_slot = entry.sampler_slot
			    };
		    }
		);
		return result;
	}

} // namespace fyuu_rhi::pipeline

namespace fyuu_rhi::shader {
	/** @brief Owned backend bytecode for one requested shader stage. */
	struct SlangCompiledEntryPoint {
		/// @brief Requested shader stage.
		fyuu_rhi::pipeline::Stage stage;
		/// @brief Source declaration name.
		std::string name;
		/// @brief Independent compiled bytecode.
		std::vector<std::byte> code;
	};

	/** @brief Owns compiler-independent output; no SDK object survives compilation. */
	struct SlangProgramData {
		/// @brief Compiled outputs in descriptor entry-point order.
		std::vector<SlangCompiledEntryPoint> entry_points;
		/// @brief Owned pipeline ABI reflection.
		fyuu_rhi::pipeline::SlangPipelineInterface interface;
		/// @brief Independent JSON reflection text.
		std::string reflection_json;
	};
} // namespace fyuu_rhi::shader

namespace fs = std::filesystem;

namespace {
	/** @brief Owns the global compiler and serializes SDK object lifetimes.
	 * @note Compilation holds mutex until all session-derived objects have been
	 * destroyed. Context refresh must acquire this same mutex, not a second
	 * cache-only lock which could race module/component/reflection use.
	 */
	struct SlangCompilerState {
		/// @brief Outlives every owned compiler object, including during normal static destruction.
		std::mutex mutex;
		/// @brief Retired only while no compilation owns a child session or reflection object.
		Slang::ComPtr<slang::IGlobalSession> global;
	};

	/** @brief Returns the process-local cache without constructing a Slang session. */
	SlangCompilerState& CompilerState() {
		static SlangCompilerState cache;
		return cache;
	}

	/** @brief Emits Debug cache/session events without constructing a compiler session.
	 * @note The DLL build tag is the SDK's session-independent version identity.
	 * No teardown callback or DLL detach cleanup is introduced by diagnostics.
	 */
	void TraceSlangEvent(std::string_view event, std::string_view key = {}) noexcept {
#if !defined(NDEBUG)
		try {
#if defined(_WIN32)
			PROCESS_MEMORY_COUNTERS_EX counters{};
			if (!GetProcessMemoryInfo(
			        GetCurrentProcess(),
			        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
			        sizeof(counters)
			    )) {
				throw std::runtime_error("Cannot sample Slang diagnostic process commitment");
			}
			fyuu_rhi::log::Debug(
			    std::format(
			        "Slang diagnostic: event={}, version={}, key={}, pid={}, private_bytes={}",
			        event,
			        spGetBuildTagString(),
			        key,
			        GetCurrentProcessId(),
			        counters.PrivateUsage
			    )
			);
#else
			fyuu_rhi::log::Debug(
			    std::format(
			        "Slang diagnostic: event={}, version={}, key={}",
			        event,
			        spGetBuildTagString(),
			        key
			    )
			);
#endif // defined(_WIN32)
		} catch (...) {
			/// @brief Diagnostic failure must not affect cache hits or compilation.
		}
#else
		(void)event;
		(void)key;
#endif // !defined(NDEBUG)
	}
	/// @brief Current disk schema; version and compiler build identity are always validated.
	constexpr std::uint32_t CACHE_SCHEMA_VERSION = 9u;

	/** @brief Lazily creates the global compiler while the lifetime guard is held. */
	slang::IGlobalSession* SlangGlobalUnderLock() {
		auto& cache = CompilerState();
		if (!cache.global) {
			auto result = slang::createGlobalSession(cache.global.writeRef());
			if (SLANG_FAILED(result) || !cache.global) {
				throw std::runtime_error(
				    std::format("Failed to create Slang global session ({})", result)
				);
			}
			TraceSlangEvent("global-created");
		}
		return cache.global.get();
	}

	/** @brief Reports SDK errors and preserves nonfatal compiler diagnostics. */
	void Check(SlangResult result, slang::IBlob* diagnostics, std::string_view operation) {
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
			fyuu_rhi::log::Warning(std::format("{}: {}", operation, message));
		}
	}

	/** @brief Hashes string length and bytes so concatenated identifiers remain distinct. */
	void HashString(boost::hash2::xxhash_64& hash, std::string_view value) {
		auto size = static_cast<std::uint64_t>(value.size());
		hash.update(&size, sizeof(size));
		hash.update(value.data(), value.size());
	}

	/** @brief Computes the bytecode and reflection integrity fingerprint. */
	std::uint64_t HashBytes(std::span<std::byte const> bytes) {
		boost::hash2::xxhash_64 hash;
		hash.update(bytes.data(), bytes.size());
		return hash.result();
	}

	/** @brief Fingerprints source dependencies without loading an entire file into memory. */
	std::uint64_t HashFile(fs::path const& path) {
		std::ifstream input(path, std::ios::binary);
		if (!input) {
			throw std::runtime_error(
			    std::format("Cannot read Slang dependency '{}'", path.string())
			);
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
			throw std::runtime_error(
			    std::format("Failed while reading Slang dependency '{}'", path.string())
			);
		}
		return hash.result();
	}

	/** @brief Identifies source, compiler build, target and options without creating a session. */
	std::string BuildCacheKey(
	    slang::TargetDesc const& target,
	    fyuu_rhi::pipeline::SlangPipelineProgramDescriptor const& desc,
	    std::string_view cache_tag,
	    std::string_view profile_name
	) {
		boost::hash2::xxhash_64 hash;
		HashString(hash, "fyuu-rhi-slang-program");
		hash.update(&CACHE_SCHEMA_VERSION, sizeof(CACHE_SCHEMA_VERSION));
		HashString(hash, spGetBuildTagString());
		HashString(hash, cache_tag);
		hash.update(&target.format, sizeof(target.format));
		if (profile_name.empty()) {
			hash.update(&target.profile, sizeof(target.profile));
		} else {

			HashString(hash, "named-profile");
			HashString(hash, profile_name);
		}
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

	/**
	 * @brief Creates a fresh module namespace for each compile miss.
	 * @note A reused ISession remembers loaded module names and can return stale
	 * source or includes. The global session remains reusable; owned ISession
	 * objects retire with compilation under the same lifetime guard.
	 */
	Slang::ComPtr<slang::ISession> CreateSession(
	    slang::TargetDesc const& target,
	    fyuu_rhi::pipeline::SlangPipelineProgramDescriptor const& desc
	) {
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
			macros.push_back({macro.name.c_str(), macro.value.c_str()});
		}

		std::array compiler_options = {
		    slang::CompilerOptionEntry{
		        slang::CompilerOptionName::Optimization,
		        {slang::CompilerOptionValueKind::Int, static_cast<int>(desc.optimization)}
		    },
		    slang::CompilerOptionEntry{
		        slang::CompilerOptionName::DebugInformation,
		        {slang::CompilerOptionValueKind::Int,
				 static_cast<std::int32_t>(
		             desc.enable_debug_info ? SLANG_DEBUG_INFO_LEVEL_MAXIMAL :
		                                      SLANG_DEBUG_INFO_LEVEL_NONE
		         )}
		    }
		};

		auto reflection_target = target;
		reflection_target.format = SLANG_SPIRV;
		reflection_target.profile = SlangGlobalUnderLock()->findProfile("spirv_1_0");
		std::array targets{target, reflection_target};
		slang::SessionDesc session_desc{};
		session_desc.targets = targets.data();
		session_desc.targetCount = static_cast<SlangInt>(targets.size());
		session_desc.searchPaths = search_paths.data();
		session_desc.searchPathCount = static_cast<SlangInt>(search_paths.size());
		session_desc.preprocessorMacros = macros.data();
		session_desc.preprocessorMacroCount = static_cast<SlangInt>(macros.size());
		session_desc.compilerOptionEntries = compiler_options.data();
		session_desc.compilerOptionEntryCount = static_cast<std::uint32_t>(compiler_options.size());
		session_desc.defaultMatrixLayoutMode = desc.matrix_layout ==
		        fyuu_rhi::pipeline::SlangPipelineProgramDescriptor::MatrixLayout::RowMajor ?
		    SLANG_MATRIX_LAYOUT_ROW_MAJOR :
		    SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

		Slang::ComPtr<slang::ISession> created;
		auto result = SlangGlobalUnderLock()->createSession(session_desc, created.writeRef());
		Check(result, nullptr, "Creating Slang session");
		TraceSlangEvent("session-created");

		return created;
	}

	/** @brief Reads a complete cache payload; missing or short reads reject the entry. */
	bool ReadBinary(fs::path const& path, std::vector<std::byte>& output) {
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
		return input && static_cast<std::size_t>(input.gcount()) == output.size();
	}

	/** @brief Rejects cached programs when included source files change. */
	bool ValidateDependencies(nlohmann::json const& manifest) {
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

	/** @brief Serializes the current binding ABI and native register assignments. */
	nlohmann::json SerializeInterface(fyuu_rhi::pipeline::SlangPipelineInterface const& interface) {
		nlohmann::json result{
		    {"bindings", nlohmann::json::array()},
		    {"vertex_inputs", nlohmann::json::array()},
		    {"fragment_outputs", nlohmann::json::array()},
		    {"push_constants", nlohmann::json::array()}
		};
		for (auto const& entry : interface.bindings) {
			nlohmann::json flags = nlohmann::json::array();
			auto words = entry.flags.Snapshot();
			for (auto word : words) {
				flags.push_back(word);
			}
			result["bindings"].push_back(
			    {{"name", entry.name},
				 {"flags", std::move(flags)},
				 {"binding", entry.slot},
				 {"space", entry.space},
				 {"resource_binding", entry.resource_slot},
				 {"resource_space", entry.resource_space},
				 {"sampler_binding", entry.sampler_slot},
				 {"sampler_space", entry.sampler_space},
				 {"count", entry.count},
				 {"visibility", entry.visibility}}
			);
		}
		for (auto const& input : interface.vertex_inputs) {
			result["vertex_inputs"].push_back(
			    {{"name", input.name},
				 {"location", input.location},
				 {"semantic_name", input.semantic_name},
				 {"semantic_index", input.semantic_index}}
			);
		}
		for (auto const& output : interface.fragment_outputs) {
			result["fragment_outputs"].push_back(
			    {{"name", output.name}, {"location", output.location}}
			);
		}
		for (auto const& range : interface.push_constants) {
			result["push_constants"].push_back(
			    {{"offset", range.offset},
				 {"size", range.size},
				 {"binding", range.abi_slot},
				 {"space", range.abi_space},
				 {"register_binding", range.register_slot},
				 {"register_space", range.register_space},
				 {"visibility", range.visibility}}
			);
		}
		return result;
	}

	/** @brief Restores the current interface schema without compatibility defaults. */
	fyuu_rhi::pipeline::SlangPipelineInterface DeserializeInterface(nlohmann::json const& json) {
		fyuu_rhi::pipeline::SlangPipelineInterface result;
		for (auto const& item : json.at("bindings")) {
			fyuu_rhi::pipeline::SlangPipelineBinding entry;
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
			entry.resource_slot = item.at("resource_binding").get<std::uint32_t>();
			entry.resource_space = item.at("resource_space").get<std::uint32_t>();
			entry.sampler_slot = item.at("sampler_binding").get<std::uint32_t>();
			entry.sampler_space = item.at("sampler_space").get<std::uint32_t>();
			entry.count = item.at("count").get<std::uint32_t>();
			entry.visibility = item.at("visibility").get<std::uint32_t>();
			result.bindings.push_back(std::move(entry));
		}
		for (auto const& item : json.at("vertex_inputs")) {
			result.vertex_inputs.push_back(
			    {item.at("name").get<std::string>(),
				 item.at("location").get<std::uint32_t>(),
				 item.at("semantic_name").get<std::string>(),
				 item.at("semantic_index").get<std::uint32_t>()}
			);
		}
		for (auto const& item : json.at("fragment_outputs")) {
			result.fragment_outputs.push_back(
			    {item.at("name").get<std::string>(), item.at("location").get<std::uint32_t>()}
			);
		}
		for (auto const& item : json.at("push_constants")) {
			result.push_constants.push_back(
			    {item.at("offset").get<std::uint32_t>(),
				 item.at("size").get<std::uint32_t>(),
				 item.at("binding").get<std::uint32_t>(),
				 item.at("space").get<std::uint32_t>(),
				 item.at("register_binding").get<std::uint32_t>(),
				 item.at("register_space").get<std::uint32_t>(),
				 item.at("visibility").get<std::uint32_t>()}
			);
		}
		return result;
	}

	/** @brief Collects the requested stages used for binding visibility. */
	std::uint32_t StageVisibility(fyuu_rhi::pipeline::SlangPipelineProgramDescriptor const& desc) {
		std::uint32_t result = 0;
		for (auto const& entry : desc.entry_points) {
			result |= 1u << static_cast<std::uint32_t>(entry.stage);
		}
		return result;
	}

	/** @brief Rejects unknown, unbounded and overflowing reflection values. */
	std::uint32_t CheckedUint32(std::size_t value, std::string_view subject) {
		if (value == SLANG_UNKNOWN_SIZE || value == SLANG_UNBOUNDED_SIZE ||
		    value > std::numeric_limits<std::uint32_t>::max()) {
			throw std::runtime_error(
			    std::format("Slang reported an unsupported {} value ({})", subject, value)
			);
		}
		return static_cast<std::uint32_t>(value);
	}

	/** @brief Returns the finite descriptor count of a scalar or resource array. */
	std::uint32_t BindingCount(slang::TypeLayoutReflection* type) {
		if (!type || !type->isArray()) {
			return 1;
		}
		return CheckedUint32(type->getTotalArrayElementCount(), "resource array size");
	}

	/** @brief Separates resource shape from shader read/write and sampler properties. */
	fyuu_rhi::ResourceFlags BindingFlags(
	    slang::ParameterCategory category,
	    slang::TypeLayoutReflection* type
	) {
		fyuu_rhi::ResourceFlags flags;
		auto leaf = type ? type->unwrapArray() : nullptr;
		auto binding_type = leaf && leaf->getBindingRangeCount() != 0 ?
		    leaf->getBindingRangeType(0) :
		    slang::BindingType::Unknown;
		switch (binding_type) {
			case slang::BindingType::Sampler:
				flags.Set(fyuu_rhi::ResourceFlagBits::SamplerBinding);
				return flags;
			case slang::BindingType::ConstantBuffer:
			case slang::BindingType::ParameterBlock:
			case slang::BindingType::InlineUniformData:
				flags.Set(fyuu_rhi::ResourceFlagBits::UniformBuffer);
				return flags;
			case slang::BindingType::Texture:
			case slang::BindingType::InputRenderTarget:
				flags.Set(fyuu_rhi::ResourceFlagBits::TextureBinding);
				return flags;
			case slang::BindingType::MutableTexture:
				flags.Set(fyuu_rhi::ResourceFlagBits::StorageBinding);
				return flags;
			case slang::BindingType::TypedBuffer:
			case slang::BindingType::RawBuffer:
				flags.Set(fyuu_rhi::ResourceFlagBits::StorageBuffer);
				flags.Set(fyuu_rhi::ResourceFlagBits::TextureBinding);
				return flags;
			case slang::BindingType::MutableTypedBuffer:
			case slang::BindingType::MutableRawBuffer:
				flags.Set(fyuu_rhi::ResourceFlagBits::StorageBuffer);
				flags.Set(fyuu_rhi::ResourceFlagBits::StorageBinding);
				return flags;
			case slang::BindingType::CombinedTextureSampler:
				flags.Set(fyuu_rhi::ResourceFlagBits::TextureBinding);
				flags.Set(fyuu_rhi::ResourceFlagBits::SamplerBinding);
				return flags;
			default:
				break;
		}

		switch (category) {
			case slang::ParameterCategory::ConstantBuffer:
				flags.Set(fyuu_rhi::ResourceFlagBits::UniformBuffer);
				break;
			case slang::ParameterCategory::SamplerState:
				flags.Set(fyuu_rhi::ResourceFlagBits::SamplerBinding);
				break;
			case slang::ParameterCategory::ShaderResource:
			case slang::ParameterCategory::UnorderedAccess: {
				auto shape = leaf ? leaf->getResourceShape() & SLANG_RESOURCE_BASE_SHAPE_MASK :
				                    SLANG_RESOURCE_NONE;
				if (shape == SLANG_STRUCTURED_BUFFER || shape == SLANG_BYTE_ADDRESS_BUFFER) {
					flags.Set(fyuu_rhi::ResourceFlagBits::StorageBuffer);
				}
				flags.Set(
				    category == slang::ParameterCategory::ShaderResource ?
				        fyuu_rhi::ResourceFlagBits::TextureBinding :
				        fyuu_rhi::ResourceFlagBits::StorageBinding
				);
				break;
			}
			default:
				break;
		}
		return flags;
	}

	/** @brief Tests whether the target layout exposes the requested register category. */
	bool HasCategory(slang::VariableLayoutReflection* variable, slang::ParameterCategory category) {
		for (unsigned index = 0u; index < variable->getCategoryCount(); ++index) {
			if (variable->getCategoryByIndex(index) == category) {
				return true;
			}
		}
		return false;
	}

	/** @brief Uses a target register offset when that category is available. */
	std::uint32_t BindingSlot(
	    slang::VariableLayoutReflection* variable,
	    slang::ParameterCategory category,
	    std::uint32_t fallback
	) {
		if (!HasCategory(variable, category)) {
			return fallback;
		}
		return CheckedUint32(variable->getOffset(category), "resource binding");
	}

	/** @brief Uses a target register space when that category is available. */
	std::uint32_t BindingSpace(
	    slang::VariableLayoutReflection* variable,
	    slang::ParameterCategory category,
	    std::uint32_t fallback
	) {
		if (!HasCategory(variable, category)) {
			return fallback;
		}
		return CheckedUint32(variable->getBindingSpace(category), "resource binding space");
	}

	/** @brief Assigns unique logical slots without conflating native register classes. */
	void UniquifyLogicalSlots(std::vector<fyuu_rhi::pipeline::SlangPipelineBinding>& bindings) {
		std::vector<std::pair<std::uint32_t, std::uint32_t>> taken;
		taken.reserve(bindings.size());
		for (auto& entry : bindings) {
			auto const Used = [&taken](std::uint32_t space, std::uint32_t slot) {
				return std::ranges::find(taken, std::pair{space, slot}) != taken.end();
			};
			if (!Used(entry.space, entry.slot)) {
				taken.emplace_back(entry.space, entry.slot);
				continue;
			}
			std::uint32_t candidate = 0u;
			while (Used(entry.space, candidate)) {
				++candidate;
			}
			entry.slot = candidate;
			taken.emplace_back(entry.space, candidate);
		}
	}

	/** @brief Selects the native register class from reflected access properties. */
	slang::ParameterCategory ResourceCategory(fyuu_rhi::ResourceFlags const& flags) {
		using Bits = fyuu_rhi::ResourceFlagBits;
		if (flags.Test(Bits::UniformBuffer)) {
			return slang::ParameterCategory::ConstantBuffer;
		}
		if (flags.Test(Bits::StorageBuffer) || flags.Test(Bits::StorageBinding)) {
			return slang::ParameterCategory::UnorderedAccess;
		}
		return slang::ParameterCategory::ShaderResource;
	}

	/** @brief Recursively records vertex inputs and fragment outputs. */
	void ReflectVarying(
	    slang::VariableLayoutReflection* variable,
	    bool input,
	    fyuu_rhi::pipeline::SlangPipelineInterface& interface
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
			    {name,
				 location,
				 semantic,
				 CheckedUint32(variable->getSemanticIndex(), "semantic index")}
			);
		} else {
			interface.fragment_outputs.push_back({name, location});
		}
	}

	/** @brief Matches neutral and native layouts by parameter name. */
	slang::VariableLayoutReflection* FindNativeParameter(
	    slang::ProgramLayout* native_layout,
	    char const* name
	) {
		if (!native_layout || !name) {
			return nullptr;
		}
		for (unsigned index = 0; index < native_layout->getParameterCount(); ++index) {
			auto candidate = native_layout->getParameterByIndex(index);
			if (!candidate || !candidate->getName()) {
				continue;
			}
			if (std::string_view(candidate->getName()) == std::string_view(name)) {
				return candidate;
			}
		}
		return nullptr;
	}

	/** @brief Resolves the register read by the target's pipeline-constant emulation. */
	bool NativePushConstantRegister(
	    slang::ProgramLayout* native_layout,
	    slang::VariableLayoutReflection* variable,
	    std::uint32_t& slot,
	    std::uint32_t& space
	) {
		auto candidate = FindNativeParameter(native_layout, variable->getName());
		if (!candidate) {
			return false;
		}
		slot = CheckedUint32(candidate->getBindingIndex(), "pipeline-constant register");
		space = CheckedUint32(candidate->getBindingSpace(), "pipeline-constant register space");
		return true;
	}

	/** @brief Combines neutral SPIR-V binding identities with native target registers. */
	fyuu_rhi::pipeline::SlangPipelineInterface ReflectInterface(
	    slang::ProgramLayout* layout,
	    slang::ProgramLayout* native_layout,
	    fyuu_rhi::pipeline::SlangPipelineProgramDescriptor const& desc
	) {
		fyuu_rhi::pipeline::SlangPipelineInterface result;
		auto visibility = StageVisibility(desc);

		for (unsigned index = 0; index < layout->getParameterCount(); ++index) {
			auto variable = layout->getParameterByIndex(index);
			if (!variable || !variable->getTypeLayout()) {
				continue;
			}
			auto category = variable->getCategory();
			bool push_constant = std::ranges::any_of(
			    std::views::iota(0u, variable->getCategoryCount()),
			    [variable](unsigned category_index) {
				    return variable->getCategoryByIndex(category_index) ==
				        slang::ParameterCategory::PushConstantBuffer;
			    }
			);
			if (push_constant) {

				auto binding_index = variable->getBindingIndex();
				auto binding_space = variable->getBindingSpace();
				auto abi_slot = binding_index == ~0u ?
				    static_cast<std::uint32_t>(result.push_constants.size()) :
				    binding_index;
				auto abi_space = binding_space == ~0u ? 0u : binding_space;
				std::uint32_t register_slot = abi_slot;
				std::uint32_t register_space = abi_space;
				NativePushConstantRegister(native_layout, variable, register_slot, register_space);
				auto constant_type = variable->getTypeLayout()->getElementTypeLayout();
				if (!constant_type) {
					constant_type = variable->getTypeLayout();
				}
				result.push_constants.push_back(
				    {CheckedUint32(
				         variable->getOffset(slang::ParameterCategory::PushConstantBuffer),
				         "push constant offset"
				     ),
					 CheckedUint32(
				         constant_type->getSize(slang::ParameterCategory::Uniform),
				         "push constant size"
				     ),
					 abi_slot,
					 abi_space,
					 register_slot,
					 register_space,
					 visibility}
				);
				continue;
			}

			auto flags = BindingFlags(category, variable->getTypeLayout());
			if (flags.None()) {
				continue;
			}
			auto slot = CheckedUint32(variable->getBindingIndex(), "resource binding");
			auto space = CheckedUint32(variable->getBindingSpace(), "resource binding space");
			auto resource_category = ResourceCategory(flags);

			auto* native_variable = FindNativeParameter(native_layout, variable->getName());
			if (!native_variable) {
				native_variable = variable;
			}
			auto resource_slot = BindingSlot(native_variable, resource_category, slot);
			auto resource_space = BindingSpace(native_variable, resource_category, space);

			auto sampler_slot =
			    BindingSlot(native_variable, slang::ParameterCategory::SamplerState, slot);
			auto sampler_space =
			    BindingSpace(native_variable, slang::ParameterCategory::SamplerState, space);
			result.bindings.push_back(
			    {.name = variable->getName() ? variable->getName() : "",
				 .flags = std::move(flags),
				 .slot = slot,
				 .space = space,
				 .resource_slot = resource_slot,
				 .resource_space = resource_space,
				 .sampler_slot = sampler_slot,
				 .sampler_space = sampler_space,
				 .count = BindingCount(variable->getTypeLayout()),
				 .visibility = visibility}
			);
		}

		UniquifyLogicalSlots(result.bindings);

		for (SlangUInt index = 0; index < layout->getEntryPointCount(); ++index) {
			auto entry = layout->getEntryPointByIndex(index);
			if (!entry || index >= desc.entry_points.size()) {
				continue;
			}
			auto stage = desc.entry_points[index].stage;
			if (stage == fyuu_rhi::pipeline::Stage::Vertex) {
				for (
				    unsigned parameter_index = 0; parameter_index < entry->getParameterCount();
				    ++parameter_index
				) {
					auto parameter = entry->getParameterByIndex(parameter_index);
					if (parameter &&
					    parameter->getCategory() == slang::ParameterCategory::VaryingInput) {
						ReflectVarying(parameter, true, result);
					}
				}
			} else if (stage == fyuu_rhi::pipeline::Stage::Fragment) {
				ReflectVarying(entry->getResultVarLayout(), false, result);
			}
		}
		return result;
	}

	/** @brief Accepts only complete payloads matching schema, build, key and dependencies. */
	bool TryLoadCache(
	    fs::path const& directory,
	    std::string_view key,
	    fyuu_rhi::shader::SlangProgramData& program
	) {
		try {
			std::ifstream manifest_file(directory / "manifest.json", std::ios::binary);
			if (!manifest_file) {
				return false;
			}

			nlohmann::json manifest;
			manifest_file >> manifest;
			if (manifest.at("schema").get<std::uint32_t>() != CACHE_SCHEMA_VERSION ||
			    manifest.at("key").get<std::string>() != key ||
			    manifest.at("slang").get<std::string>() != spGetBuildTagString() ||
			    !ValidateDependencies(manifest)) {
				return false;
			}

			std::vector<fyuu_rhi::shader::SlangCompiledEntryPoint> loaded_entries;
			for (auto const& entry : manifest.at("entry_points")) {
				std::vector<std::byte> code;
				if (!ReadBinary(directory / entry.at("file").get<std::string>(), code)) {
					return false;
				}
				if (code.size() != entry.at("size").get<std::size_t>() ||
				    HashBytes(code) != entry.at("hash").get<std::uint64_t>()) {
					return false;
				}
				loaded_entries.push_back(
				    {static_cast<fyuu_rhi::pipeline::Stage>(entry.at("stage").get<std::uint32_t>()),
					 entry.at("name").get<std::string>(),
					 std::move(code)}
				);
			}

			std::ifstream reflection_file(
			    directory / manifest.at("reflection").get<std::string>(),
			    std::ios::binary
			);
			if (!reflection_file) {
				return false;
			}
			std::string reflection{
			    std::istreambuf_iterator<char>(reflection_file),
			    std::istreambuf_iterator<char>()
			};
			if (reflection.size() != manifest.at("reflection_size").get<std::size_t>() ||
			    HashBytes(std::as_bytes(std::span(reflection))) !=
			        manifest.at("reflection_hash").get<std::uint64_t>()) {
				return false;
			}

			std::ifstream interface_file(
			    directory / manifest.at("interface").get<std::string>(),
			    std::ios::binary
			);
			if (!interface_file) {
				return false;
			}
			nlohmann::json serialized_interface;
			interface_file >> serialized_interface;

			auto interface = DeserializeInterface(serialized_interface);
			program = {std::move(loaded_entries), std::move(interface), std::move(reflection)};
			return true;
		} catch (std::exception const& error) {
			fyuu_rhi::log::Warning(
			    std::format(
			        "Ignoring invalid Slang cache '{}': {}",
			        directory.string(),
			        error.what()
			    )
			);
			return false;
		}
	}

	/** @brief Converts cache publication failure into one program-level cache diagnostic. */
	void WriteFileAtomically(fs::path const& path, std::span<std::byte const> bytes) {
		if (!fyuu_rhi::cache::WriteFileAtomically(path, bytes)) {
			throw std::runtime_error(
			    std::format("Cannot publish Slang cache file '{}'", path.string())
			);
		}
	}

	/** @brief Writes owned output and dependencies; manifest publication marks completion. */
	void WriteCache(
	    fs::path const& directory,
	    std::string_view key,
	    std::span<Slang::ComPtr<slang::IModule> const> modules,
	    fyuu_rhi::pipeline::SlangPipelineProgramDescriptor const& descriptor,
	    std::string_view cache_tag,
	    fyuu_rhi::shader::SlangProgramData const& program
	) {
		try {
			nlohmann::json manifest{
			    {"schema", CACHE_SCHEMA_VERSION},
			    {"key", key},
			    {"slang", spGetBuildTagString()},
			    {"cache_tag", cache_tag},
			    {"modules", nlohmann::json::array()},
			    {"interface", "interface.json"},
			    {"reflection", "reflection.json"},
			    {"reflection_size", program.reflection_json.size()},
			    {"reflection_hash", HashBytes(std::as_bytes(std::span(program.reflection_json)))},
			    {"entry_points", nlohmann::json::array()},
			    {"dependencies", nlohmann::json::array()}
			};

			for (auto const& module : descriptor.modules) {
				manifest["modules"].push_back(module.name);
			}
			for (std::size_t index = 0; index < program.entry_points.size(); ++index) {
				auto const& entry = program.entry_points[index];
				auto filename =
				    std::format("entry-{}-{}.bin", index, fyuu_rhi::cache::PathName(entry.name));
				WriteFileAtomically(directory / filename, entry.code);
				manifest["entry_points"].push_back(
				    {{"name", entry.name},
					 {"stage", static_cast<std::uint32_t>(entry.stage)},
					 {"file", filename},
					 {"size", entry.code.size()},
					 {"hash", HashBytes(entry.code)}}
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
				manifest["dependencies"].push_back({{"path", path}, {"hash", hash}});
			}

			auto serialized_interface = SerializeInterface(program.interface).dump(2);
			WriteFileAtomically(
			    directory / "interface.json",
			    std::as_bytes(std::span(serialized_interface))
			);
			WriteFileAtomically(
			    directory / "reflection.json",
			    std::as_bytes(std::span(program.reflection_json))
			);

			auto serialized_manifest = manifest.dump(2);
			WriteFileAtomically(
			    directory / "manifest.json",
			    std::as_bytes(std::span(serialized_manifest))
			);
		} catch (std::exception const& error) {
			fyuu_rhi::log::Warning(
			    std::format(
			        "Failed to write Slang cache '{}': {}",
			        directory.string(),
			        error.what()
			    )
			);
		}
	}

	/** @brief Compiles and copies every SDK-derived output while the lifetime guard is held. */
	fyuu_rhi::shader::SlangProgramData CompileProgram(
	    slang::TargetDesc const& target,
	    fyuu_rhi::pipeline::SlangPipelineProgramDescriptor const& desc,
	    char const* profile_name,
	    fs::path const& cache_directory,
	    std::string_view cache_key,
	    std::string_view cache_tag
	) {
		fyuu_rhi::shader::SlangProgramData program;
		auto resolved_target = target;
		if (profile_name && *profile_name != '\0') {
			resolved_target.profile = SlangGlobalUnderLock()->findProfile(profile_name);
			if (resolved_target.profile == SLANG_PROFILE_UNKNOWN) {
				throw std::invalid_argument(
				    std::format("Unknown Slang profile '{}'", profile_name)
				);
			}
		}
		auto session = CreateSession(resolved_target, desc);
		Slang::ComPtr<slang::IBlob> diagnostics;
		std::vector<Slang::ComPtr<slang::IModule>> modules;
		std::vector<Slang::ComPtr<slang::IEntryPoint>> entries;
		std::vector<slang::IComponentType*> components;

		for (auto const& module_desc : desc.modules) {
			auto path = module_desc.name + ".slang";
			Slang::ComPtr<slang::IModule> module(session->loadModuleFromSourceString(
			    module_desc.name.c_str(),
			    path.c_str(),
			    module_desc.source.c_str(),
			    diagnostics.writeRef()
			));
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

		program.entry_points.reserve(desc.entry_points.size());
		for (std::size_t index = 0; index < desc.entry_points.size(); ++index) {
			Slang::ComPtr<slang::IBlob> code;
			result = linked_program->getEntryPointCode(
			    static_cast<SlangInt>(index),
			    0,
			    code.writeRef(),
			    diagnostics.writeRef()
			);
			Check(
			    result,
			    diagnostics,
			    std::format("Generating '{}'", desc.entry_points[index].name)
			);
			auto begin = static_cast<std::byte const*>(code->getBufferPointer());
			program.entry_points.push_back(
			    {desc.entry_points[index].stage,
				 desc.entry_points[index].name,
				 {begin, begin + code->getBufferSize()}}
			);
		}

		auto reflection = linked_program->getLayout(1, diagnostics.writeRef());
		if (!reflection) {
			Check(SLANG_FAIL, diagnostics, "Reflecting Slang program");
		}
		Slang::ComPtr<slang::IBlob> native_diagnostics;
		auto native_layout = linked_program->getLayout(0, native_diagnostics.writeRef());
		program.interface = ReflectInterface(reflection, native_layout, desc);
		Slang::ComPtr<ISlangBlob> reflection_blob;
		result = reflection->toJson(reflection_blob.writeRef());
		Check(result, nullptr, "Serializing Slang reflection");
		program.reflection_json.assign(
		    static_cast<char const*>(reflection_blob->getBufferPointer()),
		    reflection_blob->getBufferSize()
		);

		WriteCache(cache_directory, cache_key, modules, desc, cache_tag, program);
		return program;
	}

	/** @brief Validates a request, attempts a session-free cache hit, then compiles on a miss. */
	fyuu_rhi::shader::SlangProgramData BuildProgram(
	    slang::TargetDesc const& target,
	    fyuu_rhi::pipeline::SlangPipelineProgramDescriptor const& descriptor,
	    std::string_view cache_tag,
	    char const* profile_name
	) {
		if (descriptor.modules.empty() || descriptor.entry_points.empty()) {
			throw std::invalid_argument("Slang program requires modules and entry points");
		}
		if (std::ranges::any_of(descriptor.modules, [](auto const& module) {
			    return module.name.empty();
		    })) {
			throw std::invalid_argument("Slang module has no name");
		}
		auto key = BuildCacheKey(
		    target,
		    descriptor,
		    cache_tag,
		    profile_name ? std::string_view(profile_name) : std::string_view{}
		);
		auto backend = cache_tag.starts_with("opengl-") ? "opengl" :
		    target.format == SLANG_DXIL                 ? "d3d12" :
		    target.format == SLANG_SPIRV                ? "vulkan" :
		    target.format == SLANG_WGSL                 ? "webgpu" :
		    target.format == SLANG_METAL                ? "metal" :
		                                                  "other";
		auto directory =
		    fyuu_rhi::cache::GetCacheDirectory(key, backend, descriptor.modules.front().name);
		std::unique_lock lock(CompilerState().mutex);
		fyuu_rhi::shader::SlangProgramData program;
		if (TryLoadCache(directory, key, program)) {
			TraceSlangEvent("cache-hit", key);
			return program;
		}
		TraceSlangEvent("cache-miss", key);
		TraceSlangEvent("compile-begin", key);
		program = CompileProgram(target, descriptor, profile_name, directory, key, cache_tag);
		TraceSlangEvent("compile-end", key);
		return program;
	}
} // namespace

namespace fyuu_rhi::shader {

	/** @brief Drains compilation and retires sessions before their global owner; reconstruction is lazy. */
	void RefreshSlang() {
		{
			auto& cache = CompilerState();
			std::unique_lock lock(cache.mutex);
			cache.global.setNull();
		}
		TraceSlangEvent("compiler-cache-refreshed");
	}

	/** @brief Immutable owned compilation output, restored from validated disk cache when possible. */
	class SlangProgram {
		/// @brief Independent bytecode and reflection; never contains SDK pointers.
		SlangProgramData m_data;

	public:
		/** @brief Loads or compiles a program without retaining compiler or reflection handles. */
		SlangProgram(
		    slang::TargetDesc const& target,
		    fyuu_rhi::pipeline::SlangPipelineProgramDescriptor const& descriptor,
		    std::string_view cache_tag,
		    char const* profile_name = nullptr
		) : m_data(BuildProgram(target, descriptor, cache_tag, profile_name)) {
		}

		/// @brief Returns independent bytecode in requested entry-point order.
		std::span<SlangCompiledEntryPoint const> GetEntryPoints() const noexcept {
			return m_data.entry_points;
		}

		/// @brief Returns the backend-neutral ABI and native register assignments.
		fyuu_rhi::pipeline::SlangPipelineInterface const& GetInterface() const noexcept {
			return m_data.interface;
		}

		/// @brief Returns the owned reflection JSON used by diagnostics and tooling.
		std::string_view GetReflectionJSON() const noexcept {
			return m_data.reflection_json;
		}
	};
} // namespace fyuu_rhi::shader
