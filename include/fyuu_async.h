#pragma once

#include "api_macro.h"
#include "fyuu_types.h"

#if defined(__cplusplus)
extern "C" {
#endif // defined(__cplusplus)

	typedef struct Fyuu_AsyncOperation_T* Fyuu_AsyncOperation;

	LIB_API Fyuu_Bool LIB_CALL Fyuu_AsyncOperationIsDone(Fyuu_AsyncOperation operation) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_AsyncOperationGetResult(Fyuu_AsyncOperation operation) NOEXCEPT;

	LIB_API Fyuu_Result LIB_CALL Fyuu_AsyncOperationWait(Fyuu_AsyncOperation operation) NOEXCEPT;

	LIB_API Fyuu_StringView LIB_CALL Fyuu_AsyncOperationGetError(Fyuu_AsyncOperation operation) NOEXCEPT;

	LIB_API void LIB_CALL Fyuu_AsyncOperationRelease(Fyuu_AsyncOperation operation) NOEXCEPT;

#if defined(__cplusplus)
}
#endif // defined(__cplusplus)
