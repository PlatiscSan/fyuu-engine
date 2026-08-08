module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <memory>
#include <stdexcept>
#endif // !defined(__cpp_lib_modules)

export module fyuu_engine:scene;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
export import fyuu_asset;
import plastic.serial_task;

export namespace fyuu_engine {

	using Scene = fyuu_asset::Scene;
	using SceneAsset = fyuu_asset::Asset<Scene>;

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
