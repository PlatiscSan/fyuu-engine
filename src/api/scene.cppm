module;
#include <version>
#if !defined(__cpp_lib_modules)
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <new>
#include <stdexcept>
#include <utility>
#include <string>
#include <filesystem>
#include <algorithm>
#endif // !defined(__cpp_lib_modules)
#include "fyuu_scene.h"

module fyuu_engine:scene_api;
#if defined(__cpp_lib_modules)
import std;
#endif // defined(__cpp_lib_modules)
import :asset_api;
import :async_api;
import :scene;

struct Fyuu_SceneAsset_T {
	std::atomic_size_t references = 1u;
	Fyuu_AssetStore store;
	fyuu_engine::SceneAsset::ManagedAsset asset;

	Fyuu_SceneAsset_T(
		Fyuu_AssetStore asset_store,
		fyuu_engine::SceneAsset::ManagedAsset&& scene_asset
	) : store(asset_store),
		asset(std::move(scene_asset)) {
		fyuu_engine::api::RetainAssetStore(store);
	}

	~Fyuu_SceneAsset_T() {
		asset.reset();
		fyuu_engine::api::ReleaseAssetStore(store);
	}
};

namespace {

	void ToAssetUUID(Fyuu_UUID const& source, fyuu_asset::UUID& output) noexcept {
		fyuu_asset::UUIDFromBytes(source.bytes, output);
	}

	void FromAssetUUID(fyuu_asset::UUID const& source, Fyuu_UUID& output) noexcept {
		fyuu_asset::UUIDToBytes(source, output.bytes);
	}

	void ClearUUID(Fyuu_UUID& output) noexcept {
		std::memset(output.bytes, 0, sizeof(output.bytes));
	}

	[[nodiscard]] bool ValidName(Fyuu_StringView const& name) noexcept {
		return name.data
			&& name.size != 0u
			&& std::memchr(name.data, '\0', name.size) == nullptr;
	}

	[[nodiscard]] bool ValidTransform(Fyuu_Transform const& transform) noexcept {
		return std::isfinite(transform.translation.x)
			&& std::isfinite(transform.translation.y)
			&& std::isfinite(transform.translation.z)
			&& std::isfinite(transform.rotation.x)
			&& std::isfinite(transform.rotation.y)
			&& std::isfinite(transform.rotation.z)
			&& std::isfinite(transform.rotation.w)
			&& std::isfinite(transform.scale.x)
			&& std::isfinite(transform.scale.y)
			&& std::isfinite(transform.scale.z);
	}

	fyuu_engine::Scene::Entity* FindEntity(
		fyuu_engine::Scene& scene,
		fyuu_asset::UUID const& id
	) noexcept {
		auto found = std::ranges::find_if(
			scene.entities,
			[&id](fyuu_engine::Scene::Entity const& entity) {
				return fyuu_asset::UUIDEqual(entity.id, id);
			}
		);
		return found == scene.entities.end() ? nullptr : &*found;
	}

	Fyuu_Result FailureResult() noexcept {
		try {
			throw;
		}
		catch (std::invalid_argument const&) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		catch (std::out_of_range const&) {
			return FYUU_ERROR_NOT_FOUND;
		}
		catch (std::filesystem::filesystem_error const&) {
			return FYUU_ERROR_IO;
		}
		catch (std::bad_alloc const&) {
			return FYUU_ERROR_OUT_OF_MEMORY;
		}
		catch (...) {
			return FYUU_ERROR_OPERATION_FAILED;
		}
	}

	void ToAssetTransform(
		Fyuu_Transform const& source,
		fyuu_engine::Scene::Entity& output
	) noexcept {
		output.translation = { source.translation.x, source.translation.y, source.translation.z };
		output.rotation = { source.rotation.x, source.rotation.y, source.rotation.z, source.rotation.w };
		output.scale = { source.scale.x, source.scale.y, source.scale.z };
	}

	void FromAssetTransform(
		fyuu_engine::Scene::Entity const& source,
		Fyuu_Transform& output
	) noexcept {
		output = {
			.translation = { source.translation[0], source.translation[1], source.translation[2] },
			.rotation = { source.rotation[0], source.rotation[1], source.rotation[2], source.rotation[3] },
			.scale = { source.scale[0], source.scale[1], source.scale[2] }
		};
	}

}

extern "C" {

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneAssetCreate(
		Fyuu_AssetStore store,
		Fyuu_SceneAsset* output
	) NOEXCEPT {
		if (!output) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		*output = nullptr;
		if (!store) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		try {
			auto asset = fyuu_engine::SceneAsset::Create(fyuu_engine::Scene{});
			*output = new Fyuu_SceneAsset_T{ store, std::move(asset) };
			return FYUU_SUCCESS;
		}
		catch (...) {
			return FailureResult();
		}
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneAssetLoad(
		Fyuu_AssetStore store,
		Fyuu_UUID id,
		Fyuu_SceneAsset* output
	) NOEXCEPT {
		if (!output) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		*output = nullptr;
		if (!store) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		try {
			fyuu_asset::UUID asset_id;
			ToAssetUUID(id, asset_id);
			if (fyuu_asset::UUIDIsNil(asset_id)) {
				return FYUU_ERROR_INVALID_ARGUMENT;
			}
			auto asset = fyuu_asset::execution::AssetLoader{}.Load<fyuu_engine::Scene>(asset_id);
			*output = new Fyuu_SceneAsset_T{ store, std::move(asset) };
			return FYUU_SUCCESS;
		}
		catch (...) {
			return FailureResult();
		}
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneAssetGetID(
		Fyuu_SceneAsset scene,
		Fyuu_UUID* output
	) NOEXCEPT {
		if (!output) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		ClearUUID(*output);
		if (!scene) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		FromAssetUUID(scene->asset->GetID(), *output);
		return FYUU_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneAssetSave(
		Fyuu_SceneAsset scene,
		Fyuu_AsyncOperation* output
	) NOEXCEPT {
		if (!output) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		*output = nullptr;
		if (!scene) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		try {
			if (!scene->asset->Get().Valid()) {
				return FYUU_ERROR_INVALID_STATE;
			}
			auto operation = scene->asset->Save();
			*output = fyuu_engine::api::CreateAsyncOperation(std::move(operation));
			return FYUU_SUCCESS;
		}
		catch (...) {
			return FailureResult();
		}
	}

	LIB_API void LIB_CALL Fyuu_SceneAssetRetain(Fyuu_SceneAsset scene) NOEXCEPT {
		if (scene) {
			scene->references.fetch_add(1u, std::memory_order_relaxed);
		}
	}

	LIB_API void LIB_CALL Fyuu_SceneAssetRelease(Fyuu_SceneAsset scene) NOEXCEPT {
		if (scene && scene->references.fetch_sub(1u, std::memory_order_acq_rel) == 1u) {
			delete scene;
		}
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneAssetValidate(Fyuu_SceneAsset scene) NOEXCEPT {
		if (!scene) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		try {
			return scene->asset->Get().Valid()
				? FYUU_SUCCESS
				: FYUU_ERROR_INVALID_STATE;
		}
		catch (...) {
			return FailureResult();
		}
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneAssetGetEntityCount(
		Fyuu_SceneAsset scene,
		size_t* output
	) NOEXCEPT {
		if (!output) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		*output = 0u;
		if (!scene) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		*output = scene->asset->Get().entities.size();
		return FYUU_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneAssetGetEntityAt(
		Fyuu_SceneAsset scene,
		size_t index,
		Fyuu_UUID* output
	) NOEXCEPT {
		if (!output) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		ClearUUID(*output);
		if (!scene) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		auto const& entities = scene->asset->Get().entities;
		if (index >= entities.size()) {
			return FYUU_ERROR_NOT_FOUND;
		}
		FromAssetUUID(entities[index].id, *output);
		return FYUU_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityCreate(
		Fyuu_SceneAsset scene,
		Fyuu_SceneEntityDescriptor const* descriptor,
		Fyuu_UUID* output
	) NOEXCEPT {
		if (!output) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		ClearUUID(*output);
		if (!scene || !descriptor || !ValidName(descriptor->name) || !ValidTransform(descriptor->transform)) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		try {
			auto& data = scene->asset->Get();
			fyuu_asset::UUID parent;
			fyuu_asset::UUID mesh;
			fyuu_asset::UUID material;
			ToAssetUUID(descriptor->parent, parent);
			ToAssetUUID(descriptor->mesh, mesh);
			ToAssetUUID(descriptor->material, material);
			if (!fyuu_asset::UUIDIsNil(parent) && !FindEntity(data, parent)) {
				return FYUU_ERROR_NOT_FOUND;
			}

			std::string name{ descriptor->name.data, descriptor->name.size };
			auto& entity = data.CreateEntity(name);
			entity.parent = parent;
			entity.mesh = mesh;
			entity.material = material;
			ToAssetTransform(descriptor->transform, entity);
			FromAssetUUID(entity.id, *output);
			return FYUU_SUCCESS;
		}
		catch (...) {
			return FailureResult();
		}
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityDestroy(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity
	) NOEXCEPT {
		if (!scene) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		fyuu_asset::UUID id;
		ToAssetUUID(entity, id);
		auto& entities = scene->asset->Get().entities;
		if (std::ranges::any_of(entities, [&id](auto const& candidate) {
			return fyuu_asset::UUIDEqual(candidate.parent, id);
		})) {
			return FYUU_ERROR_INVALID_STATE;
		}
		auto removed = std::erase_if(entities, [&id](auto const& candidate) {
			return fyuu_asset::UUIDEqual(candidate.id, id);
		});
		return removed == 0u ? FYUU_ERROR_NOT_FOUND : FYUU_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityExists(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_Bool* output
	) NOEXCEPT {
		if (!output) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		*output = false;
		if (!scene) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		fyuu_asset::UUID id;
		ToAssetUUID(entity, id);
		if (fyuu_asset::UUIDIsNil(id)) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		*output = FindEntity(scene->asset->Get(), id) != nullptr;
		return FYUU_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityGetName(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		char* buffer,
		size_t capacity,
		size_t* required_size
	) NOEXCEPT {
		if (!required_size) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		*required_size = 0u;
		if (!scene) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		fyuu_asset::UUID id;
		ToAssetUUID(entity, id);
		auto* found = FindEntity(scene->asset->Get(), id);
		if (!found) {
			return FYUU_ERROR_NOT_FOUND;
		}
		*required_size = found->name.size() + 1u;
		if (!buffer || capacity < *required_size) {
			return FYUU_ERROR_INSUFFICIENT_BUFFER;
		}
		std::memcpy(buffer, found->name.data(), found->name.size());
		buffer[found->name.size()] = '\0';
		return FYUU_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntitySetName(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_StringView name
	) NOEXCEPT {
		if (!scene || !ValidName(name)) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		try {
			fyuu_asset::UUID id;
			ToAssetUUID(entity, id);
			auto* found = FindEntity(scene->asset->Get(), id);
			if (!found) {
				return FYUU_ERROR_NOT_FOUND;
			}
			found->name.assign(name.data, name.size);
			return FYUU_SUCCESS;
		}
		catch (...) {
			return FailureResult();
		}
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityGetParent(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_UUID* output
	) NOEXCEPT {
		if (!output) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		ClearUUID(*output);
		if (!scene) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		fyuu_asset::UUID id;
		ToAssetUUID(entity, id);
		auto* found = FindEntity(scene->asset->Get(), id);
		if (!found) {
			return FYUU_ERROR_NOT_FOUND;
		}
		FromAssetUUID(found->parent, *output);
		return FYUU_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntitySetParent(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_UUID parent
	) NOEXCEPT {
		if (!scene) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		fyuu_asset::UUID id;
		fyuu_asset::UUID parent_id;
		ToAssetUUID(entity, id);
		ToAssetUUID(parent, parent_id);
		auto& data = scene->asset->Get();
		auto* found = FindEntity(data, id);
		if (!found) {
			return FYUU_ERROR_NOT_FOUND;
		}
		if (fyuu_asset::UUIDEqual(id, parent_id)) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		if (!fyuu_asset::UUIDIsNil(parent_id) && !FindEntity(data, parent_id)) {
			return FYUU_ERROR_NOT_FOUND;
		}

		auto current = parent_id;
		for (std::size_t depth = 0u; !fyuu_asset::UUIDIsNil(current); ++depth) {
			if (depth >= data.entities.size()) {
				return FYUU_ERROR_INVALID_STATE;
			}
			if (fyuu_asset::UUIDEqual(current, id)) {
				return FYUU_ERROR_INVALID_STATE;
			}
			auto* ancestor = FindEntity(data, current);
			if (!ancestor) {
				return FYUU_ERROR_NOT_FOUND;
			}
			current = ancestor->parent;
		}
		found->parent = parent_id;
		return FYUU_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityGetTransform(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_Transform* output
	) NOEXCEPT {
		if (!output) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		*output = {};
		if (!scene) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		fyuu_asset::UUID id;
		ToAssetUUID(entity, id);
		auto* found = FindEntity(scene->asset->Get(), id);
		if (!found) {
			return FYUU_ERROR_NOT_FOUND;
		}
		FromAssetTransform(*found, *output);
		return FYUU_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntitySetTransform(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_Transform const* transform
	) NOEXCEPT {
		if (!scene || !transform || !ValidTransform(*transform)) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		fyuu_asset::UUID id;
		ToAssetUUID(entity, id);
		auto* found = FindEntity(scene->asset->Get(), id);
		if (!found) {
			return FYUU_ERROR_NOT_FOUND;
		}
		ToAssetTransform(*transform, *found);
		return FYUU_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityGetMesh(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_UUID* output
	) NOEXCEPT {
		if (!output) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		ClearUUID(*output);
		if (!scene) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		fyuu_asset::UUID id;
		ToAssetUUID(entity, id);
		auto* found = FindEntity(scene->asset->Get(), id);
		if (!found) {
			return FYUU_ERROR_NOT_FOUND;
		}
		FromAssetUUID(found->mesh, *output);
		return FYUU_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntitySetMesh(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_UUID mesh
	) NOEXCEPT {
		if (!scene) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		fyuu_asset::UUID id;
		fyuu_asset::UUID mesh_id;
		ToAssetUUID(entity, id);
		ToAssetUUID(mesh, mesh_id);
		auto* found = FindEntity(scene->asset->Get(), id);
		if (!found) {
			return FYUU_ERROR_NOT_FOUND;
		}
		found->mesh = mesh_id;
		return FYUU_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityGetMaterial(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_UUID* output
	) NOEXCEPT {
		if (!output) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		ClearUUID(*output);
		if (!scene) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		fyuu_asset::UUID id;
		ToAssetUUID(entity, id);
		auto* found = FindEntity(scene->asset->Get(), id);
		if (!found) {
			return FYUU_ERROR_NOT_FOUND;
		}
		FromAssetUUID(found->material, *output);
		return FYUU_SUCCESS;
	}

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntitySetMaterial(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_UUID material
	) NOEXCEPT {
		if (!scene) {
			return FYUU_ERROR_INVALID_ARGUMENT;
		}
		fyuu_asset::UUID id;
		fyuu_asset::UUID material_id;
		ToAssetUUID(entity, id);
		ToAssetUUID(material, material_id);
		auto* found = FindEntity(scene->asset->Get(), id);
		if (!found) {
			return FYUU_ERROR_NOT_FOUND;
		}
		found->material = material_id;
		return FYUU_SUCCESS;
	}

}
