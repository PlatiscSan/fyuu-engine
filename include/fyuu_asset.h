#pragma once

#include "api_macro.h"
#include "fyuu_types.h"

#if defined(__cplusplus)
extern "C" {
#endif // defined(__cplusplus)

	typedef struct Fyuu_AssetStore_T* Fyuu_AssetStore;

	typedef struct Fyuu_AssetStoreDescriptor {
		Fyuu_StringView root;
	} Fyuu_AssetStoreDescriptor;

	LIB_API Fyuu_Result LIB_CALL Fyuu_AssetStoreCreate(
		Fyuu_AssetStoreDescriptor const* descriptor,
		Fyuu_AssetStore* output
	) NOEXCEPT;

	LIB_API void LIB_CALL Fyuu_AssetStoreRelease(Fyuu_AssetStore store) NOEXCEPT;

#if defined(__cplusplus)
}
#endif // defined(__cplusplus)
