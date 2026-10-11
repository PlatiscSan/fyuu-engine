#include <version>
#if !defined(__cpp_lib_modules)
#include <cstddef>
#include <stdexcept>
#include <utility>

#include <cstring>
#include <string>

#include <fstream>
#include <iostream>

#include <cstdint>

#include <array>

#include <chrono>

#include <thread>

#include <system_error>

#include <variant>

#include <filesystem>

#include <coroutine>
#endif // !defined(__cpp_lib_modules)
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_asset;
import plastic.serial_task;

namespace {

	/** @brief Removes the temporary import tree when the test leaves its scope. */
	class TemporaryDirectory final {
	private:
		std::filesystem::path m_path; ///< Test-owned directory eligible for recursive removal.

	public:
		explicit TemporaryDirectory(std::filesystem::path const& path) : m_path(path) {
			std::filesystem::remove_all(m_path);
			std::filesystem::create_directories(m_path);
		}

		TemporaryDirectory(TemporaryDirectory const&) = delete;
		TemporaryDirectory& operator=(TemporaryDirectory const&) = delete;

		~TemporaryDirectory() {
			std::error_code error;
			std::filesystem::remove_all(m_path, error);
		}

		[[nodiscard]] std::filesystem::path const& GetPath() const noexcept {
			return m_path;
		}
	};

	/**
	 * @brief Rejects a failed test condition.
	 * @param condition Condition required by the test.
	 * @param message Failure description.
	 */
	void Require(bool condition, char const* message) {
		if (!condition) {
			throw std::runtime_error{message};
		}
	}

	/** @brief Writes a complete text fixture. */
	void WriteText(std::filesystem::path const& path, std::string const& text) {
		std::ofstream output{path, std::ios::binary | std::ios::trunc};
		output.write(text.data(), static_cast<std::streamsize>(text.size()));
		if (!output) {
			throw std::runtime_error{"Failed to write test fixture"};
		}
	}

	/** @brief Reads one complete text fixture. */
	[[nodiscard]] std::string ReadText(std::filesystem::path const& path) {
		auto const size = std::filesystem::file_size(path);
		std::string result(static_cast<std::size_t>(size), '\0');
		std::ifstream input{path, std::ios::binary};
		if (!input || !input.read(result.data(), static_cast<std::streamsize>(result.size()))) {
			throw std::runtime_error{"Failed to read test fixture"};
		}
		return result;
	}

	/** @brief Converts explicit UTF-8 test data into a native filesystem path. */
	[[nodiscard]] std::filesystem::path TestPathFromUTF8(std::string_view path) {
		std::u8string encoded;
		encoded.assign(
		    reinterpret_cast<char8_t const*>(path.data()),
		    reinterpret_cast<char8_t const*>(path.data() + path.size())
		);
		return std::filesystem::path{encoded};
	}

	/** @brief Adapts the language-level Importer awaitable to the test task API. */
	[[nodiscard]] plastic::concurrency::SerialTask<fyuu_asset::Object> AwaitImport(
	    std::string_view type,
	    std::filesystem::path const& source
	) {
		co_return co_await fyuu_asset::Importer{type, source};
	}

	/** @brief Adapts the native Factory create awaitable to the test task API. */
	[[nodiscard]] plastic::concurrency::SerialTask<fyuu_asset::Asset> AwaitCreate(
	    std::string_view type,
	    std::string_view name,
	    fyuu_asset::Object const& fields
	) {
		co_return co_await fyuu_asset::Factory{type, name, fields};
	}

	/** @brief Adapts the UUID Factory load awaitable to the test task API. */
	[[nodiscard]] plastic::concurrency::SerialTask<fyuu_asset::Asset> AwaitLoad(
	    fyuu_asset::UUID const& uuid
	) {
		co_return co_await fyuu_asset::Factory{uuid};
	}

	/** @brief Adapts the named Factory load awaitable to the test task API. */
	[[nodiscard]] plastic::concurrency::SerialTask<fyuu_asset::Asset> AwaitLoad(
	    std::string_view type,
	    std::string_view name
	) {
		co_return co_await fyuu_asset::Factory{type, name};
	}

	/** @brief Adapts the UUID Deleter awaitable to the test task API. */
	[[nodiscard]] plastic::concurrency::SerialTask<fyuu_asset::UUID> AwaitDelete(
	    fyuu_asset::UUID const& uuid
	) {
		co_return co_await fyuu_asset::Deleter{uuid};
	}

	/** @brief Adapts the named Deleter awaitable to the test task API. */
	[[nodiscard]] plastic::concurrency::SerialTask<fyuu_asset::UUID> AwaitDelete(
	    std::string_view type,
	    std::string_view name
	) {
		co_return co_await fyuu_asset::Deleter{type, name};
	}

	/** @brief Owns an imported test asset while preserving the task-style test syntax. */
	class ImportedAsset final {
	private:
		fyuu_asset::Asset m_asset; ///< Asset created from the parser output.

	public:
		explicit ImportedAsset(fyuu_asset::Asset&& asset) : m_asset(std::move(asset)) {}

		/** @brief Preserves the existing task-style synchronization point. */
		void Wait() const noexcept {}

		/** @brief Returns the imported asset. */
		[[nodiscard]] fyuu_asset::Asset* Result() noexcept {
			return &m_asset;
		}
	};

	/**
	 * @brief Parses a source and creates an asset from the caller-selected final fields.
	 * @param source Source file, directory, or archive.
	 * @param type Runtime asset type.
	 * @param name Name unique within the runtime type.
	 * @param fields Application-defined fields appended to the parser output.
	 * @return Created asset owned by the test helper.
	 */
	[[nodiscard]] ImportedAsset ImportAsset(
	    std::filesystem::path const& source,
	    std::string_view type,
	    std::string_view name,
	    fyuu_asset::Object const& fields = {}
	) {
		auto parsed = AwaitImport(type, source);
		parsed.Wait();
		auto result = std::move(*parsed.Result());
		for (auto const& [field_name, value] : fields) {
			if (!result.emplace(field_name, value).second) {
				throw std::invalid_argument{"Caller field conflicts with parser output"};
			}
		}
		auto created = AwaitCreate(type, name, result);
		created.Wait();
		return ImportedAsset{std::move(*created.Result())};
	}

	/** @brief Writes a two-by-two opaque BGRA TGA texture fixture. */
	void WriteTGA(std::filesystem::path const& path) {
		std::array<unsigned char, 34u> bytes{};
		bytes[2] = 2u;
		bytes[12] = 2u;
		bytes[14] = 2u;
		bytes[16] = 32u;
		bytes[17] = 0x28u;
		for (std::size_t offset = 18u; offset < bytes.size(); offset += 4u) {
			bytes[offset] = static_cast<unsigned char>(offset * 3u);
			bytes[offset + 1u] = static_cast<unsigned char>(offset * 5u);
			bytes[offset + 2u] = static_cast<unsigned char>(offset * 7u);
			bytes[offset + 3u] = 0xffu;
		}
		std::ofstream output{path, std::ios::binary | std::ios::trunc};
		output.write(reinterpret_cast<char const*>(bytes.data()), bytes.size());
		if (!output) {
			throw std::runtime_error{"Failed to write TGA test fixture"};
		}
	}

	/** @brief Writes a minimal structurally valid PMD fixture. */
	void WritePMD(std::filesystem::path const& path) {
		std::ofstream output{path, std::ios::binary | std::ios::trunc};
		auto WriteValue = [&output]<class T>(T const& value) {
			output.write(reinterpret_cast<char const*>(&value), sizeof(T));
		};
		output.write("Pmd", 3);
		WriteValue(1.0f);
		WriteValue(std::array<char, 20u>{});
		WriteValue(std::array<char, 256u>{});
		WriteValue(std::uint32_t{3u});
		WriteValue(std::array<char, 38u>{});
		WriteValue(std::array<char, 38u>{});
		WriteValue(std::array<char, 38u>{});
		WriteValue(std::uint32_t{3u});
		WriteValue(std::array<std::uint16_t, 3u>{0u, 1u, 2u});
		WriteValue(std::uint32_t{1u});
		std::array<char, 70u> material{};
		auto const index_count = std::uint32_t{3u};
		std::memcpy(material.data() + 46u, &index_count, sizeof(index_count));
		WriteValue(material);
		if (!output) {
			throw std::runtime_error{"Failed to write PMD test fixture"};
		}
	}

	/** @brief Imports a JSON file and verifies dynamic scalar conversion. */
	void TestJSON(std::filesystem::path const& root) {
		auto const source = root / "configuration.json";
		WriteText(source, R"({"enabled":true,"count":7})");

		auto task = ImportAsset(source, "configuration", "primary");
		task.Wait();
		auto* asset = task.Result();
		Require(asset, "JSON import did not return an asset");
		Require(
		    fyuu_asset::FormatUUID(asset->GetUUID()) !=
		        "00000000-0000-0000-0000-000000000000",
		    "Imported asset UUID is nil"
		);
		Require(asset->GetType() == "configuration", "Imported asset type is incorrect");
		Require(asset->GetName() == "primary", "Imported asset name is incorrect");
		Require(
		    asset->GetField("enabled").As<bool>(),
		    "JSON boolean was not imported"
		);
		Require(
		    asset->GetField("count").As<std::uint64_t>() == 7u,
		    "JSON unsigned integer was not imported"
		);

		auto copied = *asset;
		Require(copied.GetField("enabled").As<bool>(), "Asset copy lost its fields");
		auto moved = std::move(copied);
		Require(moved.GetField("count").As<std::uint64_t>() == 7u, "Asset move lost its fields");
		auto assigned = *asset;
		assigned = std::move(moved);
		Require(
		    assigned.GetField("count").As<std::uint64_t>() == 7u,
		    "Asset move assignment lost its fields"
		);
	}

	/** @brief Keeps parser output independent from the application-selected asset schema. */
	void TestSeparatedImportAndCreate(std::filesystem::path const& root) {
		auto const source = root / "dynamic.json";
		WriteText(source, R"({"value":11})");

		auto parsed = AwaitImport("configuration", source);
		parsed.Wait();
		fyuu_asset::Object fields{
		    {"payload", fyuu_asset::Value{std::move(*parsed.Result())}},
		    {"label", fyuu_asset::Value{std::string{"custom"}}}
		};
		auto created = AwaitCreate("configuration", "dynamic", fields);
		created.Wait();
		auto* asset = created.Result();
		auto const& payload = asset->GetField("payload").As<fyuu_asset::Object>();
		Require(
		    payload.at("value").As<std::uint64_t>() == 11u,
		    "Application-selected asset field lost its parsed value"
		);
		Require(
		    asset->GetField("label").As<std::string>() == "custom",
		    "Application-selected asset field was not persisted"
		);
		bool flattened = true;
		try {
			static_cast<void>(asset->GetField("value"));
		} catch (std::out_of_range const&) {
			flattened = false;
		}
		Require(!flattened, "Parser output leaked into the dynamic Asset schema");
	}

	/** @brief Imports an unknown format and verifies lossless binary fallback. */
	void TestBinary(std::filesystem::path const& root) {
		auto const source = root / "payload.unknown";
		WriteText(source, "Fyuu");

		auto task = ImportAsset(source, "binary", "payload");
		task.Wait();
		auto const& binary = task.Result()->GetField("binary").As<fyuu_asset::Binary>();
		Require(binary.size() == 4u, "Binary fallback changed the source size");
		Require(binary.front() == std::byte{'F'}, "Binary fallback changed the source bytes");
	}

	/** @brief Detects JSON content without relying on its filename extension. */
	void TestFormatDetection(std::filesystem::path const& root) {
		auto const source = root / "configuration.data";
		WriteText(source, "\xef\xbb\xbf  {\"detected\":true}");

		auto task = ImportAsset(source, "configuration", "detected");
		task.Wait();
		Require(
		    task.Result()->GetField("detected").As<bool>(),
		    "JSON signature detection depended on the filename extension"
		);
	}

	/** @brief Imports minimal OBJ geometry through the Assimp VFS adapter. */
	void TestModel(std::filesystem::path const& root) {
		auto const source = root / "triangle.obj";
		WriteText(
		    source,
		    "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
		    "vt 0.25 0.5\nvt 0.75 0.5\nvt 0.25 1.0\n"
		    "f 1/1 2/2 3/3\n"
		);

		auto task = ImportAsset(source, "model", "triangle");
		task.Wait();
		auto const& meshes = task.Result()->GetField("meshes").As<fyuu_asset::Array>();
		Require(meshes.size() == 1u, "OBJ import produced an incorrect mesh count");
		auto const& mesh = meshes.front().As<fyuu_asset::Object>();
		Require(
		    mesh.at("vertex_count").As<std::uint64_t>() == 3u,
		    "OBJ import produced an incorrect vertex count"
		);
		auto const& texcoords = mesh.at("texcoords").As<fyuu_asset::Object>();
		auto const offset = texcoords.at("offset").As<std::uint64_t>();
		auto const size = texcoords.at("size").As<std::uint64_t>();
		auto const& geometry = task.Result()->GetField("geometry").As<fyuu_asset::File>().m_data;
		Require(size == 6u * sizeof(float), "OBJ import produced an incorrect UV size");
		Require(offset + size <= geometry.size(), "OBJ import produced an invalid UV range");
		std::array<float, 6u> imported_uvs{};
		std::memcpy(imported_uvs.data(), geometry.data() + offset, size);
		Require(
		    imported_uvs == std::array<float, 6u>{0.25f, 0.5f, 0.75f, 0.5f, 0.25f, 1.0f},
		    "OBJ import changed the UV values"
		);
	}

	/** @brief Imports PMD geometry through the internal MMDLoader-backed path. */
	void TestPMD(std::filesystem::path const& root) {
		auto const source = root / "triangle.pmd";
		WritePMD(source);

		auto task = ImportAsset(source, "model", "pmd");
		task.Wait();
		Require(
		    task.Result()->GetField("meshes").As<fyuu_asset::Array>().size() == 1u,
		    "PMD import produced an incorrect mesh count"
		);
		Require(
		    task.Result()->GetField("materials").As<fyuu_asset::Array>().size() == 1u,
		    "PMD import produced an incorrect material count"
		);
	}

	/** @brief Imports a directory package through its asset.json entry declaration. */
	void TestPackage(std::filesystem::path const& root) {
		auto const package = root / "package";
		std::filesystem::create_directory(package);
		WriteText(package / "asset.json", R"({"entry":"data.json"})");
		WriteText(package / "data.json", R"({"value":19})");

		auto task = ImportAsset(package, "package", "directory");
		task.Wait();
		Require(
		    task.Result()->GetField("value").As<std::uint64_t>() == 19u,
		    "Directory package selected the wrong entry"
		);
	}

	/** @brief Preserves UTF-8 across virtual lookup and native persistence boundaries. */
	void TestUnicodeVirtualPath(std::filesystem::path const& root) {
		auto const filename = std::string{"\xe6\x95\xb0\xe6\x8d\xae.json"};
		auto const type = std::string{"\xe9\x85\x8d\xe7\xbd\xae"};
		auto const name = std::string{"\xe7\xa4\xba\xe4\xbe\x8b"};
		auto const package = root / "unicode_virtual_path";
		std::filesystem::create_directory(package);
		WriteText(package / "asset.json", "{\"entry\":\"" + filename + "\"}");
		WriteText(package / TestPathFromUTF8(filename), R"({"value":23})");

		auto task = ImportAsset(package, type, name);
		task.Wait();
		Require(
		    task.Result()->GetField("value").As<std::uint64_t>() == 23u,
		    "UTF-8 virtual path selected an incorrect source"
		);
		Require(
		    std::filesystem::is_regular_file(
		        root / TestPathFromUTF8(name) / "asset.json"
		    ),
		    "UTF-8 asset identity was not persisted through the native filesystem"
		);
	}

	/** @brief Discovers the only supported model when a package has no manifest. */
	void TestManifestlessPackage(std::filesystem::path const& root) {
		auto const package = root / "manifestless";
		std::filesystem::create_directory(package);
		WriteText(package / "readme.txt", "Package documentation");
		WriteText(package / "triangle.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

		auto task = ImportAsset(package, "model", "manifestless");
		task.Wait();
		Require(
		    task.Result()->GetField("meshes").As<fyuu_asset::Array>().size() == 1u,
		    "Manifestless package did not select its only supported model"
		);
	}

	/** @brief Imports every supported model found in one manifestless package. */
	void TestMultipleModelPackage(std::filesystem::path const& root) {
		auto const package = root / "multiple_models";
		std::filesystem::create_directory(package);
		WriteText(package / "second.obj", "v 0 0 0\nv 2 0 0\nv 0 2 0\nf 1 2 3\n");
		WriteText(package / "first.obj", "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

		auto task = ImportAsset(package, "model", "multiple_models");
		task.Wait();
		auto const& models = task.Result()->GetField("models").As<fyuu_asset::Array>();
		Require(models.size() == 2u, "Multi-model package lost a model entry");
		for (std::size_t index = 0u; index < models.size(); ++index) {
			auto const& model = models[index].As<fyuu_asset::Object>();
			Require(
			    model.at("name").As<std::string>() == "model_" + std::to_string(index),
			    "Multi-model package produced an unstable model name"
			);
			Require(
			    model.at("meshes").As<fyuu_asset::Array>().size() == 1u,
			    "Multi-model package produced an incorrect mesh count"
			);
		}
	}

	/** @brief Relocates an absolute model texture path by its packaged filename. */
	void TestRelocatedTexture(std::filesystem::path const& root) {
		auto const package = root / "relocated_texture";
		std::filesystem::create_directory(package);
		WriteText(
		    package / "triangle.obj",
		    "mtllib material.mtl\n"
		    "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
		    "vt 0 0\nvt 1 0\nvt 0 1\n"
		    "usemtl surface\nf 1/1 2/2 3/3\n"
		);
		WriteText(
		    package / "material.mtl",
		    "newmtl surface\nmap_Kd C:\\original\\texture.tga\n"
		);
		WriteTGA(package / "texture.tga");

		auto task = ImportAsset(package, "model", "relocated_texture");
		task.Wait();
		auto const& textures = task.Result()->GetField("textures").As<fyuu_asset::Array>();
		Require(textures.size() == 1u, "Absolute texture path was not relocated into the package");
		auto const& texture = textures.front().As<fyuu_asset::Object>();
		Require(
		    texture.at("name").As<std::string>() == "texture.tga",
		    "Relocated texture lost its filename"
		);
		Require(
		    texture.at("width").As<std::uint64_t>() == 2u &&
		        texture.at("height").As<std::uint64_t>() == 2u,
		    "Imported texture dimensions are incorrect"
		);
		Require(
		    texture.at("format").As<std::string>() == "bc1_rgba_unorm_srgb",
		    "Opaque color texture did not use sRGB BC1"
		);
		Require(
		    texture.at("mip_levels").As<fyuu_asset::Array>().size() == 2u,
		    "Imported texture did not contain a complete mip chain"
		);
		Require(
		    texture.at("pixels").As<fyuu_asset::File>().m_data.size() == 16u,
		    "Imported BC1 texture has an incorrect byte size"
		);
	}

	/** @brief Loads persistent dynamic fields through a catalog UUID. */
	void TestLoadByUUID(std::filesystem::path const& root) {
		auto const source = root / "catalogued.json";
		WriteText(source, R"({"loaded":true,"value":31})");
		auto imported = ImportAsset(source, "configuration", "catalogued");
		imported.Wait();
		auto const uuid = imported.Result()->GetUUID();
		auto const uuid_text = fyuu_asset::FormatUUID(uuid);
		Require(
		    std::filesystem::is_regular_file(root / "catalog.json"),
		    "Successful import did not immediately persist the catalog"
		);
		Require(
		    std::filesystem::is_regular_file(root / "catalogued" / "asset.json"),
		    "Successful import did not immediately persist the asset"
		);
		auto const catalog = ReadText(root / "catalog.json");
		Require(
		    catalog.contains(uuid_text) && catalog.contains("configuration") &&
		        catalog.contains("catalogued"),
		    "Immediately persisted catalog lost the imported identity"
		);

		auto task = AwaitLoad(uuid);
		task.Wait();
		auto* asset = task.Result();
		Require(asset, "UUID load did not return an asset");
		Require(
		    fyuu_asset::FormatUUID(asset->GetUUID()) == uuid_text,
		    "UUID load changed the asset identity"
		);
		Require(asset->GetType() == "configuration", "UUID load lost the catalog type");
		Require(asset->GetName() == "catalogued", "UUID load lost the catalog name");
		Require(
		    asset->GetField("loaded").As<bool>(),
		    "UUID load did not read persistent fields"
		);
		Require(
		    asset->GetField("value").As<std::uint64_t>() == 31u,
		    "UUID load returned an incorrect field value"
		);

		auto named_task = AwaitLoad("configuration", "catalogued");
		named_task.Wait();
		auto* named_asset = named_task.Result();
		Require(named_asset, "Named load did not return an asset");
		Require(
		    fyuu_asset::FormatUUID(named_asset->GetUUID()) == uuid_text,
		    "Named load resolved an incorrect UUID"
		);
		Require(
		    named_asset->GetField("value").As<std::uint64_t>() == 31u,
		    "Named load returned an incorrect field value"
		);
		/** @brief Different runtime types cannot overwrite a shared independent asset directory. */
		bool rejected = false;
		try {
			static_cast<void>(ImportAsset(source, "another_type", "catalogued"));
		}
		catch (std::invalid_argument const&) { rejected = true; }
		Require(rejected, "Different types overwrote the same asset directory");
	}

	/** @brief Verifies destruction persistence and typed deserialization as one round trip. */
	void TestPersistenceRoundTrip(std::filesystem::path const& root) {
		auto const source = root / "roundtrip.unknown";
		WriteText(source, "Binary payload");
		auto const reference = fyuu_asset::ParseUUID(
		    "522d3fbb-c34c-4100-9425-8e447fbdc86f"
		);
		std::string uuid_text;
		{
			auto task = ImportAsset(
			    source,
			    "binary",
			    "roundtrip",
			    fyuu_asset::Object{
			        {"preview",
			         fyuu_asset::Value{fyuu_asset::File{
			             .m_name = "preview.jpg",
			             .m_data = {
			                 std::byte{0xff},
			                 std::byte{0xd8},
			                 std::byte{0xff}
			             }
			         }}},
			        {"reference", fyuu_asset::Value{reference}}
			    }
			);
			task.Wait();
			uuid_text = fyuu_asset::FormatUUID(task.Result()->GetUUID());
		}

		auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
		while (std::chrono::steady_clock::now() < deadline) {
			try {
				auto loaded = AwaitLoad(fyuu_asset::ParseUUID(uuid_text));
				loaded.Wait();
				auto const& binary = loaded.Result()->GetField("binary").As<fyuu_asset::Binary>();
				auto const& preview = loaded.Result()->GetField("preview").As<fyuu_asset::File>();
				Require(binary.size() == 14u, "Persisted binary field changed size");
				Require(preview.m_name == "preview.jpg", "Attached file lost its logical name");
				Require(preview.m_data.size() == 3u, "Attached file changed size");
				Require(
				    std::filesystem::is_regular_file(
				        root / "roundtrip" / "texture" / "asset_0_preview.jpg"
				    ),
				    "Attached file was not persisted beside the asset JSON"
				);
				Require(
				    fyuu_asset::FormatUUID(
				        loaded.Result()->GetField("reference").As<fyuu_asset::UUID>()
				    ) == fyuu_asset::FormatUUID(reference),
				    "Persisted UUID field changed value"
				);
				return;
			} catch (std::exception const&) {
				std::this_thread::sleep_for(std::chrono::milliseconds{10});
			}
		}
		throw std::runtime_error{"Timed out waiting for asset destruction persistence"};
	}

	/** @brief Deletes assets through both identities without allowing stale values to restore them. */
	void TestDelete(std::filesystem::path const& root) {
		auto const source = root / "deletable.json";
		WriteText(source, R"({"value":47})");
		fyuu_asset::UUID uuid;
		{
			auto imported = ImportAsset(
			    source,
			    "configuration",
			    "deletable"
			);
			imported.Wait();
			uuid = imported.Result()->GetUUID();
			auto deletion = AwaitDelete(uuid);
			deletion.Wait();
			Require(deletion.Result(), "UUID deletion did not return a result");
			Require(
			    *deletion.Result() == uuid,
			    "UUID deletion returned a different asset identity"
			);
			Require(
			    !std::filesystem::exists(root / "deletable"),
			    "UUID deletion left the independent asset directory behind"
			);
		}

		auto barrier = AwaitLoad(uuid);
		bool missing = false;
		try {
			barrier.Wait();
			static_cast<void>(barrier.Result());
		} catch (std::out_of_range const&) {
			missing = true;
		}
		Require(missing, "A stale Asset value restored a deleted catalog entry");
		Require(
		    !ReadText(root / "catalog.json").contains(fyuu_asset::FormatUUID(uuid)),
		    "UUID deletion left the catalog entry behind"
		);

		auto imported = ImportAsset(
		    source,
		    "configuration",
		    "named_deletion"
		);
		imported.Wait();
		auto const named_uuid = imported.Result()->GetUUID();
		auto deletion = AwaitDelete("configuration", "named_deletion");
		deletion.Wait();
		Require(
		    deletion.Result() && *deletion.Result() == named_uuid,
		    "Named deletion resolved a different asset identity"
		);
		Require(
		    !std::filesystem::exists(root / "named_deletion"),
		    "Named deletion left the independent asset directory behind"
		);
	}

	/** @brief Verifies that worker exceptions reach the owning SerialTask. */
	void TestFailure(std::filesystem::path const& root) {
		auto const source = root / "invalid.json";
		WriteText(source, "{");

		try {
			static_cast<void>(ImportAsset(source, "configuration", "invalid"));
		} catch (std::exception const&) {
			return;
		}
		throw std::runtime_error{"Import failure did not propagate through SerialTask"};
	}

} // namespace

int main() {
	try {
		TemporaryDirectory directory{
		    std::filesystem::current_path() / "fyuu_asset_import_tests"
		};
		fyuu_asset::InitializeAssetContext(directory.GetPath());
		fyuu_asset::Importer::RegisterBuiltIn("configuration");
		fyuu_asset::Importer::RegisterBuiltIn("binary");
		fyuu_asset::Importer::RegisterBuiltIn("model");
		fyuu_asset::Importer::RegisterBuiltIn("package");
		fyuu_asset::Importer::RegisterBuiltIn("another_type");
		fyuu_asset::Importer::RegisterBuiltIn("\xe9\x85\x8d\xe7\xbd\xae");
		TestJSON(directory.GetPath());
		TestSeparatedImportAndCreate(directory.GetPath());
		TestBinary(directory.GetPath());
		TestFormatDetection(directory.GetPath());
		TestModel(directory.GetPath());
		TestPMD(directory.GetPath());
		TestPackage(directory.GetPath());
		TestUnicodeVirtualPath(directory.GetPath());
		TestManifestlessPackage(directory.GetPath());
		TestMultipleModelPackage(directory.GetPath());
		TestRelocatedTexture(directory.GetPath());
		TestFailure(directory.GetPath());
		TestLoadByUUID(directory.GetPath());
		TestPersistenceRoundTrip(directory.GetPath());
		TestDelete(directory.GetPath());
	} catch (std::exception const& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
	return 0;
}
