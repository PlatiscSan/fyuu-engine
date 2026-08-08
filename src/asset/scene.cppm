module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <algorithm>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#endif // !defined(__cpp_lib_modules)

export module fyuu_engine:scene;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
export import fyuu_asset;
import plastic.serial_task;
import :asset;

export namespace fyuu_engine {

	using Scene = fyuu_asset::Scene;
	using SceneAsset = fyuu_asset::Asset<Scene>;

	struct SceneAssetEntry {
		fyuu_asset::UUID id;
		std::string name;
	};

	class SceneSaveOperation {
	private:
		class Implementation;
		std::unique_ptr<Implementation> m_implementation;

		explicit SceneSaveOperation(SceneAsset::ManagedAsset const& asset);

	public:
		SceneSaveOperation(SceneSaveOperation const&) = delete;
		SceneSaveOperation& operator=(SceneSaveOperation const&) = delete;
		SceneSaveOperation(SceneSaveOperation&&) noexcept;
		SceneSaveOperation& operator=(SceneSaveOperation&&) noexcept;
		~SceneSaveOperation() noexcept;

		[[nodiscard]] bool IsDone() const noexcept;
		void Wait() const;

		friend SceneSaveOperation SaveScene(SceneAsset::ManagedAsset const& asset);
	};

	[[nodiscard]] SceneSaveOperation SaveScene(SceneAsset::ManagedAsset const& asset);

	[[nodiscard]] std::vector<SceneAssetEntry> DiscoverScenes(AssetStore const& store);

	[[nodiscard]] SceneAsset::ManagedAsset LoadScene(
		AssetStore const& store,
		fyuu_asset::UUID const& id
	);

}

class fyuu_engine::SceneSaveOperation::Implementation {
public:
	plastic::concurrency::SerialTask<void> task;

	explicit Implementation(SceneAsset::ManagedAsset const& asset)
		: task(asset->Save()) {
	}
};

fyuu_engine::SceneSaveOperation::SceneSaveOperation(SceneAsset::ManagedAsset const& asset)
	: m_implementation(std::make_unique<Implementation>(asset)) {
}

fyuu_engine::SceneSaveOperation::SceneSaveOperation(SceneSaveOperation&&) noexcept = default;

fyuu_engine::SceneSaveOperation& fyuu_engine::SceneSaveOperation::operator=(SceneSaveOperation&&) noexcept = default;

fyuu_engine::SceneSaveOperation::~SceneSaveOperation() noexcept {
	if (m_implementation) {
		try {
			m_implementation->task.Wait();
		}
		catch (...) {
		}
	}
}

bool fyuu_engine::SceneSaveOperation::IsDone() const noexcept {
	return m_implementation->task.IsDone();
}

void fyuu_engine::SceneSaveOperation::Wait() const {
	m_implementation->task.Wait();
}

fyuu_engine::SceneSaveOperation fyuu_engine::SaveScene(SceneAsset::ManagedAsset const& asset) {
	if (!asset) {
		throw std::invalid_argument("Cannot save an empty scene asset");
	}
	return SceneSaveOperation{ asset };
}

std::vector<fyuu_engine::SceneAssetEntry> fyuu_engine::DiscoverScenes(AssetStore const& store) {
	std::vector<SceneAssetEntry> scenes;
	auto const directory = store.Root() / "Scene";
	std::error_code error;
	if (!std::filesystem::exists(directory, error)) {
		if (error) {
			throw std::filesystem::filesystem_error(
				"Failed to inspect scene asset directory",
				directory,
				error
			);
		}
		return scenes;
	}

	for (std::filesystem::directory_iterator iterator(directory, error), end;
		iterator != end;
		iterator.increment(error)) {
		if (error) {
			throw std::filesystem::filesystem_error(
				"Failed to enumerate scene assets",
				directory,
				error
			);
		}
		if (!iterator->is_regular_file() || iterator->path().extension() != ".json") {
			continue;
		}
		try {
			auto name = iterator->path().stem().string();
			scenes.push_back(SceneAssetEntry{
				.id = fyuu_asset::ParseUUID(name),
				.name = std::move(name)
			});
		}
		catch (...) {
		}
	}
	if (error) {
		throw std::filesystem::filesystem_error(
			"Failed to enumerate scene assets",
			directory,
			error
		);
	}

	std::ranges::sort(scenes, {}, &SceneAssetEntry::name);
	return scenes;
}

fyuu_engine::SceneAsset::ManagedAsset fyuu_engine::LoadScene(
	AssetStore const& store,
	fyuu_asset::UUID const& id
) {
	if (fyuu_asset::UUIDIsNil(id)) {
		throw std::invalid_argument("Cannot load a scene with an empty UUID");
	}
	(void)store;
	return fyuu_asset::execution::AssetLoader{}.Load<Scene>(id);
}
