#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <coroutine>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

#if !defined(__cpp_lib_reflection)
#include <boost/describe.hpp>
#endif

import fyuu_asset;

namespace test_asset {

	struct Configuration {
		int width = 0;
		std::string name;

		void Serialize(std::filesystem::path const& path) const {
			nlohmann::json document{
				{ "width", width },
				{ "name", name }
			};
			std::ofstream output(path, std::ios::binary | std::ios::trunc);
			output << document.dump(2);
			if (!output) {
				throw std::runtime_error("Failed to write test asset");
			}
		}

		[[nodiscard]] static Configuration Deserialize(std::filesystem::path const& path) {
			std::ifstream input(path, std::ios::binary);
			if (!input) {
				throw std::runtime_error("Failed to open test asset");
			}
			auto document = nlohmann::json::parse(input);
			return Configuration{
				.width = document.at("width").get<int>(),
				.name = document.at("name").get<std::string>()
			};
		}
	};

#if !defined(__cpp_lib_reflection)
	BOOST_DESCRIBE_STRUCT(
		Configuration,
		(),
		(width, name)
	)
#endif

} // namespace test_asset

namespace {

	using namespace std::chrono_literals;
	using TestAsset = fyuu_asset::Asset<test_asset::Configuration>;

	void Require(bool condition, char const* message) {
		if (!condition) {
			throw std::runtime_error(message);
		}
	}

	std::string UUIDString(fyuu_asset::UUID const& id) {
		return id.ToString();
	}

	std::filesystem::path SerializedPath(
		std::filesystem::path const& root,
		auto const& id
	) {
		return root / "Configuration" / (UUIDString(id) + ".json");
	}

	void WaitForFile(std::filesystem::path const& path) {
		auto deadline = std::chrono::steady_clock::now() + 5s;
		while (!std::filesystem::exists(path)) {
			if (std::chrono::steady_clock::now() >= deadline) {
				throw std::runtime_error("Timed out waiting for asynchronous asset serialization");
			}
			std::this_thread::sleep_for(10ms);
		}
	}

	void WaitForWidth(
		std::filesystem::path const& path,
		std::filesystem::file_time_type previous_write,
		int expected
	) {
		auto deadline = std::chrono::steady_clock::now() + 5s;
		std::error_code error;
		for (;;) {
			auto current_write = std::filesystem::last_write_time(path, error);
			if (!error && current_write != previous_write) {
				break;
			}
			error.clear();
			if (std::chrono::steady_clock::now() >= deadline) {
				throw std::runtime_error("Timed out waiting for asynchronous asset update");
			}
			std::this_thread::sleep_for(10ms);
		}

		std::ifstream input(path, std::ios::binary);
		std::string document(
			std::istreambuf_iterator<char>{ input },
			std::istreambuf_iterator<char>{}
		);
		auto width = "\"width\": " + std::to_string(expected);
		Require(document.contains(width), "Asynchronously updated width is incorrect");
	}

	struct ReceiverState {
		std::mutex mutex;
		std::condition_variable condition;
		bool completed = false;
		bool stopped = false;
	};

	struct Receiver {
		std::shared_ptr<ReceiverState> state;

		void set_value() && noexcept {
			{
				std::lock_guard lock(state->mutex);
				state->completed = true;
			}
			state->condition.notify_one();
		}

		void set_error(std::exception_ptr) && noexcept {
			std::terminate();
		}

		void set_stopped() && noexcept {
			{
				std::lock_guard lock(state->mutex);
				state->completed = true;
				state->stopped = true;
			}
			state->condition.notify_one();
		}
	};

	void TestAssetLifetimeAndSerialization(std::filesystem::path const& root) {
		auto asset = TestAsset::Create(
			test_asset::Configuration{
				.width = 1920,
				.name = "main"
			}
		);
		auto id = asset->GetID();

		Require(TestAsset::Find(id).get() == asset.get(), "Find did not return the loaded asset");
		auto path = SerializedPath(root, id);
		asset.reset();

		Require(!TestAsset::Find(id), "Asset remained registered after its last strong reference");
		WaitForFile(path);

		std::ifstream input(path, std::ios::binary);
		std::string document(
			std::istreambuf_iterator<char>{ input },
			std::istreambuf_iterator<char>{}
		);
		Require(document.contains("\"width\": 1920"), "Serialized width is incorrect");
		Require(document.contains("\"name\": \"main\""), "Serialized name is incorrect");
		input.close();

		auto loaded = fyuu_asset::execution::AssetLoader{}.Load<test_asset::Configuration>(id);
		Require(loaded->Get().width == 1920, "Loaded width is incorrect");
		Require(loaded->Get().name == "main", "Loaded name is incorrect");
		auto previous_write = std::filesystem::last_write_time(path);
		loaded->Get().width = 2560;
		loaded.reset();
		WaitForWidth(path, previous_write, 2560);
	}

	void TestManualSave(std::filesystem::path const& root) {
		auto asset = TestAsset::Create(
			test_asset::Configuration{
				.width = 800,
				.name = "manual"
			}
		);
		auto id = asset->GetID();
		auto path = SerializedPath(root, id);
		auto save = asset->Save();
		save.Wait();
		WaitForFile(path);

		Require(TestAsset::Find(id).get() == asset.get(), "Manual save unloaded the asset");
		std::ifstream input(path, std::ios::binary);
		std::string document(
			std::istreambuf_iterator<char>{ input },
			std::istreambuf_iterator<char>{}
		);
		Require(document.contains("\"width\": 800"), "Manual save wrote incorrect data");
	}

	void TestSchedule() {
		auto state = std::make_shared<ReceiverState>();
		auto sender = fyuu_asset::execution::AssetScheduler{}.schedule();
		auto operation = std::move(sender).connect(Receiver{ state });
		operation.start();

		std::unique_lock lock(state->mutex);
		Require(
			state->condition.wait_for(
				lock,
				5s,
				[&] { return state->completed; }
			),
			"Scheduled receiver did not complete"
		);
		Require(!state->stopped, "Scheduled receiver was unexpectedly stopped");
	}

	void TestBitmap(std::filesystem::path const& root) {
		using BitmapAsset = fyuu_asset::Asset<fyuu_asset::Bitmap>;
		fyuu_asset::Bitmap rgb(
			1u,
			1u,
			fyuu_asset::BitmapFormat::R8G8B8,
			std::vector<std::byte>{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 } }
		);
		fyuu_asset::Bitmap hdr(
			1u,
			1u,
			fyuu_asset::BitmapFormat::R32G32B32A32Float,
			std::vector<std::byte>(16)
		);
		Require(rgb.Valid(), "R8G8B8 bitmap is invalid");
		Require(hdr.Valid(), "R32G32B32A32Float bitmap is invalid");

		auto bitmap = BitmapAsset::Create(
			2u,
			1u,
			fyuu_asset::BitmapFormat::R8G8B8A8,
			std::vector<std::byte>{
				std::byte{ 255 }, std::byte{ 0 }, std::byte{ 0 }, std::byte{ 255 },
				std::byte{ 0 }, std::byte{ 255 }, std::byte{ 0 }, std::byte{ 255 }
			}
		);
		auto id = bitmap->GetID();
		auto path = root / "Bitmap" / (UUIDString(id) + ".json");
		Require(bitmap->Get().Valid(), "Created bitmap is invalid");
		bitmap.reset();
		WaitForFile(path);
		auto binary = path;
		binary.replace_extension(".bin");
		Require(std::filesystem::exists(binary), "Bitmap binary data was not serialized");
		{
			std::ifstream input(path, std::ios::binary);
			std::string document(
				std::istreambuf_iterator<char>{ input },
				std::istreambuf_iterator<char>{}
			);
			Require(!document.contains("pixels"), "Bitmap pixels were written to JSON");
		}

		auto loaded = fyuu_asset::execution::AssetLoader{}.Load<fyuu_asset::Bitmap>(id);
		Require(loaded->Get().Valid(), "Loaded bitmap is invalid");
		Require(loaded->Get().width == 2, "Loaded bitmap width is incorrect");
		Require(loaded->Get().height == 1, "Loaded bitmap height is incorrect");
		Require(
			loaded->Get().format == fyuu_asset::BitmapFormat::R8G8B8A8,
			"Loaded bitmap format is incorrect"
		);
		Require(loaded->Get().pixels[4] == std::byte{ 0 }, "Loaded bitmap red channel is incorrect");
		Require(loaded->Get().pixels[5] == std::byte{ 255 }, "Loaded bitmap green channel is incorrect");
	}

	void TestShader(std::filesystem::path const& root) {
		using ShaderAsset = fyuu_asset::Asset<fyuu_asset::Shader>;
		auto shader = ShaderAsset::Create(
			std::string{ "triangle" },
			std::string{ "[shader(\"vertex\")] void vertexMain() {}" }
		);
		auto id = shader->GetID();
		auto path = root / "Shader" / (UUIDString(id) + ".json");
		Require(shader->Get().Valid(), "Created shader is invalid");
		shader.reset();
		WaitForFile(path);

		auto loaded = fyuu_asset::execution::AssetLoader{}.Load<fyuu_asset::Shader>(id);
		Require(loaded->Get().Valid(), "Loaded shader is invalid");
		Require(loaded->Get().name == "triangle", "Loaded shader name is incorrect");
		Require(
			loaded->Get().source == "[shader(\"vertex\")] void vertexMain() {}",
			"Loaded shader source is incorrect"
		);
	}

	void TestPipeline(std::filesystem::path const& root) {
		using ShaderAsset = fyuu_asset::Asset<fyuu_asset::Shader>;
		using PipelineAsset = fyuu_asset::Asset<fyuu_asset::Pipeline>;
		auto vertex = ShaderAsset::Create(
			std::string{ "pipeline_vertex" },
			std::string{ "[shader(\"vertex\")] void vertexMain() {}" }
		);
		auto fragment = ShaderAsset::Create(
			std::string{ "pipeline_fragment" },
			std::string{ "[shader(\"fragment\")] void fragmentMain() {}" }
		);
		auto pipeline = PipelineAsset::Create(
			fyuu_asset::PipelineType::Graphics,
			std::vector<fyuu_asset::Pipeline::Stage>{
				{ vertex->GetID(), "vertexMain", fyuu_asset::ShaderStage::Vertex },
				{ fragment->GetID(), "fragmentMain", fyuu_asset::ShaderStage::Fragment }
			}
		);
		auto id = pipeline->GetID();
		auto path = root / "Pipeline" / (UUIDString(id) + ".json");
		Require(pipeline->Get().Valid(), "Created pipeline is invalid");
		pipeline.reset();
		WaitForFile(path);

		auto loaded = fyuu_asset::execution::AssetLoader{}.Load<fyuu_asset::Pipeline>(id);
		Require(loaded->Get().Valid(), "Loaded pipeline is invalid");
		Require(loaded->Get().stages.size() == 2, "Loaded pipeline stage count is incorrect");
		Require(
			UUIDString(loaded->Get().stages[0].shader) == UUIDString(vertex->GetID()),
			"Loaded pipeline shader reference is incorrect"
		);
	}

	void TestAudio(std::filesystem::path const& root) {
		using AudioAsset = fyuu_asset::Asset<fyuu_asset::Audio>;
		auto audio = AudioAsset::Create(
			48000u,
			std::uint16_t{ 2 },
			fyuu_asset::AudioFormat::Int16,
			std::vector<std::byte>{
				std::byte{ 0x00 }, std::byte{ 0x00 },
				std::byte{ 0xff }, std::byte{ 0x7f }
			}
		);
		auto id = audio->GetID();
		auto path = root / "Audio" / (UUIDString(id) + ".json");
		Require(audio->Get().Valid(), "Created audio is invalid");
		Require(audio->Get().FrameCount() == 1, "Created audio frame count is incorrect");
		audio.reset();
		WaitForFile(path);

		auto binary = path;
		binary.replace_extension(".bin");
		Require(std::filesystem::exists(binary), "Audio binary data was not serialized");
		auto loaded = fyuu_asset::execution::AssetLoader{}.Load<fyuu_asset::Audio>(id);
		Require(loaded->Get().Valid(), "Loaded audio is invalid");
		Require(loaded->Get().sample_rate == 48000, "Loaded audio sample rate is incorrect");
		Require(loaded->Get().channels == 2, "Loaded audio channel count is incorrect");
		Require(loaded->Get().format == fyuu_asset::AudioFormat::Int16, "Loaded audio format is incorrect");
		Require(loaded->Get().samples[3] == std::byte{ 0x7f }, "Loaded audio sample data is incorrect");
	}

	void TestMesh(std::filesystem::path const& root) {
		using MeshAsset = fyuu_asset::Asset<fyuu_asset::Mesh>;
		auto mesh = MeshAsset::Create(
			std::uint32_t{ 4 },
			std::vector<std::byte>{
				std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 },
				std::byte{ 5 }, std::byte{ 6 }, std::byte{ 7 }, std::byte{ 8 }
			},
			fyuu_asset::MeshIndexFormat::UInt16,
			std::vector<std::byte>{ std::byte{ 0 }, std::byte{ 0 }, std::byte{ 1 }, std::byte{ 0 } }
		);
		auto id = mesh->GetID();
		auto path = root / "Mesh" / (UUIDString(id) + ".json");
		Require(mesh->Get().Valid(), "Created mesh is invalid");
		mesh.reset();
		WaitForFile(path);
		auto loaded = fyuu_asset::execution::AssetLoader{}.Load<fyuu_asset::Mesh>(id);
		Require(loaded->Get().Valid(), "Loaded mesh is invalid");
		Require(loaded->Get().VertexCount() == 2, "Loaded mesh vertex count is incorrect");
		Require(loaded->Get().IndexCount() == 2, "Loaded mesh index count is incorrect");
	}

	void TestMaterial(std::filesystem::path const& root) {
		using MaterialAsset = fyuu_asset::Asset<fyuu_asset::Material>;
		auto pipeline = TestAsset::Create(test_asset::Configuration{});
		auto bitmap = TestAsset::Create(test_asset::Configuration{});
		auto material = MaterialAsset::Create(
			pipeline->GetID(),
			std::unordered_map<std::string, std::vector<float>>{
				{ "base_color", { 1.0f, 0.5f, 0.25f, 1.0f } }
			},
			std::unordered_map<std::string, decltype(bitmap->GetID())>{
				{ "albedo", bitmap->GetID() }
			}
		);
		auto id = material->GetID();
		auto path = root / "Material" / (UUIDString(id) + ".json");
		Require(material->Get().Valid(), "Created material is invalid");
		material.reset();
		WaitForFile(path);
		auto loaded = fyuu_asset::execution::AssetLoader{}.Load<fyuu_asset::Material>(id);
		Require(loaded->Get().Valid(), "Loaded material is invalid");
		Require(loaded->Get().parameters.at("base_color")[1] == 0.5f, "Loaded material parameter is incorrect");
		Require(
			UUIDString(loaded->Get().bitmaps.at("albedo")) == UUIDString(bitmap->GetID()),
			"Loaded material bitmap reference is incorrect"
		);
	}

	void TestScene(std::filesystem::path const& root) {
		using SceneAsset = fyuu_asset::Asset<fyuu_asset::Scene>;
		auto root_id = TestAsset::Create(test_asset::Configuration{});
		auto child_id = TestAsset::Create(test_asset::Configuration{});
		auto mesh_id = TestAsset::Create(test_asset::Configuration{});
		auto material_id = TestAsset::Create(test_asset::Configuration{});
		auto scene = SceneAsset::Create(
			std::vector<fyuu_asset::Scene::Entity>{
				fyuu_asset::Scene::Entity{
					.id = root_id->GetID(),
					.parent = {},
					.name = "Root",
					.translation = { 0.0f, 0.0f, 0.0f },
					.rotation = { 0.0f, 0.0f, 0.0f, 1.0f },
					.scale = { 1.0f, 1.0f, 1.0f },
					.mesh = {},
					.material = {}
				},
				fyuu_asset::Scene::Entity{
					.id = child_id->GetID(),
					.parent = root_id->GetID(),
					.name = "Child",
					.translation = { 1.0f, 2.0f, 3.0f },
					.mesh = mesh_id->GetID(),
					.material = material_id->GetID()
				}
			}
		);
		auto id = scene->GetID();
		auto path = root / "Scene" / (UUIDString(id) + ".json");
		Require(scene->Get().Valid(), "Created scene is invalid");
		scene.reset();
		WaitForFile(path);

		auto loaded = fyuu_asset::execution::AssetLoader{}.Load<fyuu_asset::Scene>(id);
		Require(loaded->Get().Valid(), "Loaded scene is invalid");
		Require(loaded->Get().entities.size() == 2, "Loaded scene entity count is incorrect");
		Require(loaded->Get().entities[1].translation[2] == 3.0f, "Loaded scene transform is incorrect");
		Require(
			UUIDString(loaded->Get().entities[1].parent) == UUIDString(root_id->GetID()),
			"Loaded scene hierarchy is incorrect"
		);
	}

} // namespace

int main() {
	auto root = std::filesystem::temp_directory_path() / "fyuu_asset_tests";
	std::error_code error;
	std::filesystem::remove_all(root, error);
	std::filesystem::create_directories(root);
	fyuu_asset::SetRoot(root);

	try {
		TestAssetLifetimeAndSerialization(root);
		TestManualSave(root);
		TestBitmap(root);
		TestShader(root);
		TestPipeline(root);
		TestAudio(root);
		TestMesh(root);
		TestMaterial(root);
		TestScene(root);
		TestSchedule();
	}
	catch (std::exception const& exception) {
		std::cerr << exception.what() << '\n';
		return EXIT_FAILURE;
	}

	std::filesystem::remove_all(root, error);
	return EXIT_SUCCESS;
}
