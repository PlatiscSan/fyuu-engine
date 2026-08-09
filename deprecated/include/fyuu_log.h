#pragma once
#include "api_macro.h"
#if defined(__cplusplus)
#include <cstdint>
#else
#include <stdint.h>
#endif // defined(__cplusplus)

#if defined(__cplusplus)
extern "C" {
#endif // defined(__cplusplus)

	LIB_API void LIB_CALL Fyuu_Trace(char const* msg, char const* file, uint_least32_t line, char const* function) NOEXCEPT;
	LIB_API void LIB_CALL Fyuu_Debug(char const* msg, char const* file, uint_least32_t line, char const* function) NOEXCEPT;
	LIB_API void LIB_CALL Fyuu_Info(char const* msg, char const* file, uint_least32_t line, char const* function) NOEXCEPT;
	LIB_API void LIB_CALL Fyuu_Warning(char const* msg, char const* file, uint_least32_t line, char const* function) NOEXCEPT;
	LIB_API void LIB_CALL Fyuu_Error(char const* msg, char const* file, uint_least32_t line, char const* function) NOEXCEPT;
	LIB_API void LIB_CALL Fyuu_Fatal(char const* msg, char const* file, uint_least32_t line, char const* function) NOEXCEPT;

#if defined(__cplusplus)
}
#endif // defined(__cplusplus)

#define FYUU_LOG_TRACE(msg) Fyuu_Trace(msg, __FILE__, __LINE__, __FUNCTION__)
#define FYUU_LOG_DEBUG(msg) Fyuu_Debug(msg, __FILE__, __LINE__, __FUNCTION__)
#define FYUU_LOG_INFO(msg) Fyuu_Info(msg, __FILE__, __LINE__, __FUNCTION__)
#define FYUU_LOG_WARNING(msg) Fyuu_Warning(msg, __FILE__, __LINE__, __FUNCTION__)
#define FYUU_LOG_ERROR(msg) Fyuu_Error(msg, __FILE__, __LINE__, __FUNCTION__)
#define FYUU_LOG_FATAL(msg) Fyuu_Fatal(msg, __FILE__, __LINE__, __FUNCTION__)
