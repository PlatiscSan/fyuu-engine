module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <array>
#include <cstdint>
#include <exception>
#include <format>
#include <string>
#include <string_view>
#include <vector>
#endif // !defined(__cpp_lib_modules)
#include <imgui.h>

module fyuu_editor:asset_browser_panel;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import fyuu_engine;
import :context;

namespace {
	using fyuu_editor::EditorContext;

	// Asset Browser call chain: EditorApplication::DrawDefaultLayout ->
	// DrawAssetBrowserPanel -> RefreshAssets/DrawAssetList -> engine discovery/ImGui.
	struct AssetBrowserState {
		std::vector<fyuu_engine::AssetEntry> scenes;
		std::vector<fyuu_engine::AssetEntry> meshes;
		std::vector<fyuu_engine::AssetEntry> materials;
		bool refresh = true;
	};

	AssetBrowserState s_asset_browser;

	// Called by DrawAssetBrowserPanel on first draw or after a refresh request.
	// Calls generic engine discovery for each category and reports errors to Console.
	void RefreshAssets(
		AssetBrowserState& state,
		EditorContext& context,
		fyuu_engine::AssetStore const& store
	) {
		try {
			state.scenes = fyuu_engine::DiscoverAssets(store, "Scene");
			state.meshes = fyuu_engine::DiscoverAssets(store, "Mesh");
			state.materials = fyuu_engine::DiscoverAssets(store, "Material");
		}
		catch (std::exception const& exception) {
			context.Log(std::format("Failed to refresh assets: {}", exception.what()));
		}
		state.refresh = false;
	}

	// Called by each category tab. Renders filtering and drag payload creation; for
	// Scene entries only, writes opened_scene on double-click for the application.
	void DrawAssetList(
		std::vector<fyuu_engine::AssetEntry> const& assets,
		fyuu_asset::UUID* opened_scene,
		char const* payload_type,
		std::string_view filter
	) {
		if (assets.empty()) {
			ImGui::TextDisabled("No assets found");
			return;
		}
		auto drew_asset = false;
		for (auto const& asset : assets) {
			if (!filter.empty() && asset.name.find(filter) == std::string::npos) {
				continue;
			}
			drew_asset = true;
			auto const activated = ImGui::Selectable(
				asset.name.c_str(),
				false,
				ImGuiSelectableFlags_AllowDoubleClick
				);
			if (ImGui::BeginDragDropSource()) {
				std::uint8_t id[16];
				asset.id.ToBytes(id);
				ImGui::SetDragDropPayload(payload_type, id, sizeof(id));
				ImGui::TextUnformatted(asset.name.c_str());
				ImGui::EndDragDropSource();
			}
			if (activated
				&& opened_scene
				&& ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
				*opened_scene = asset.id;
			}
		}
		if (!drew_asset) {
			ImGui::TextDisabled("No matching assets");
		}
	}

}

namespace fyuu_editor {

	// Called by EditorApplication::UpdateSave after a successful save; invalidates the
	// panel cache so the next DrawAssetBrowserPanel calls RefreshAssets.
	void RequestAssetBrowserRefresh() noexcept {
		s_asset_browser.refresh = true;
	}

	// Called once per frame by DrawDefaultLayout. Owns panel-local filter/cache UI,
	// calls RefreshAssets and DrawAssetList, and returns open intent via opened_scene.
	void DrawAssetBrowserPanel(
		EditorContext& context,
		fyuu_engine::AssetStore* store,
		fyuu_asset::UUID& opened_scene
	) {
		static std::array<char, 128> filter{};
		if (ImGui::Begin("Assets")) {
			if (ImGui::Button("Refresh")) {
				s_asset_browser.refresh = true;
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputTextWithHint("##AssetFilter", "Search assets", filter.data(), filter.size());
			if (!store) {
				ImGui::TextDisabled("No asset store available");
			}
			else {
				if (s_asset_browser.refresh) {
					RefreshAssets(s_asset_browser, context, *store);
				}
				auto const filter_text = std::string_view{ filter.data() };
				if (ImGui::BeginTabBar("AssetCategories")) {
					auto const scene_label = std::format(
						"Scenes ({})###Scenes",
						s_asset_browser.scenes.size()
						);
					auto const mesh_label = std::format(
						"Meshes ({})###Meshes",
						s_asset_browser.meshes.size()
						);
					auto const material_label = std::format(
						"Materials ({})###Materials",
						s_asset_browser.materials.size()
						);
					if (ImGui::BeginTabItem(scene_label.c_str())) {
						DrawAssetList(
							s_asset_browser.scenes,
							&opened_scene,
							"FYUU_SCENE_ASSET",
							filter_text
							);
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem(mesh_label.c_str())) {
						DrawAssetList(
							s_asset_browser.meshes,
							nullptr,
							"FYUU_MESH_ASSET",
							filter_text
							);
						ImGui::EndTabItem();
					}
					if (ImGui::BeginTabItem(material_label.c_str())) {
						DrawAssetList(
							s_asset_browser.materials,
							nullptr,
							"FYUU_MATERIAL_ASSET",
							filter_text
							);
						ImGui::EndTabItem();
					}
					ImGui::EndTabBar();
				}
			}
		}
		ImGui::End();
	}

}
