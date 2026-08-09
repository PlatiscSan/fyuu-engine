#pragma once

#include "api_macro.h"
#include "fyuu_types.h"
#include "fyuu_async.h"
#include "fyuu_asset.h"

#if defined(__cplusplus)
extern "C" {
#endif // defined(__cplusplus)

	typedef struct Fyuu_SceneAsset_T* Fyuu_SceneAsset;

	typedef struct Fyuu_SceneEntityDescriptor {
		Fyuu_StringView name;
		Fyuu_UUID parent;
		Fyuu_Transform transform;
		Fyuu_UUID mesh;
		Fyuu_UUID material;
	} Fyuu_SceneEntityDescriptor;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneAssetCreate(Fyuu_AssetStore store, Fyuu_SceneAsset* output) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneAssetLoad(
		Fyuu_AssetStore store,
		Fyuu_UUID id,
		Fyuu_SceneAsset* output
	) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneAssetGetID(Fyuu_SceneAsset scene, Fyuu_UUID* output) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneAssetSave(Fyuu_SceneAsset scene, Fyuu_AsyncOperation* output) NOEXCEPT;

	LIB_API void LIB_CALL Fyuu_SceneAssetRetain(Fyuu_SceneAsset scene) NOEXCEPT;

	LIB_API void LIB_CALL Fyuu_SceneAssetRelease(Fyuu_SceneAsset scene) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneAssetValidate(Fyuu_SceneAsset scene) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneAssetGetEntityCount(
		Fyuu_SceneAsset scene,
		size_t* output
	) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneAssetGetEntityAt(
		Fyuu_SceneAsset scene,
		size_t index,
		Fyuu_UUID* output
	) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityCreate(
		Fyuu_SceneAsset scene,
		Fyuu_SceneEntityDescriptor const* descriptor,
		Fyuu_UUID* output
	) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityDestroy(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity
	) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityExists(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_Bool* output
	) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityGetName(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		char* buffer,
		size_t capacity,
		size_t* required_size
	) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntitySetName(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_StringView name
	) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityGetParent(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_UUID* output
	) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntitySetParent(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_UUID parent
	) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityGetTransform(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_Transform* output
	) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntitySetTransform(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_Transform const* transform
	) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityGetMesh(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_UUID* output
	) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntitySetMesh(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_UUID mesh
	) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntityGetMaterial(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_UUID* output
	) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_SceneEntitySetMaterial(
		Fyuu_SceneAsset scene,
		Fyuu_UUID entity,
		Fyuu_UUID material
	) NOEXCEPT;

#if defined(__cplusplus)
}
#endif // defined(__cplusplus)
