#pragma once

#if defined(__cplusplus)
#include <cstddef>
#include <cstdint>
#include <cstdbool>
extern "C" {
#else
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#endif // defined(__cplusplus)

	typedef bool Fyuu_Bool;

	typedef enum Fyuu_Result {
		FYUU_SUCCESS = 0,
		FYUU_PENDING = 1,
		FYUU_ERROR_INVALID_ARGUMENT = 2,
		FYUU_ERROR_INVALID_STATE = 3,
		FYUU_ERROR_NOT_FOUND = 4,
		FYUU_ERROR_INSUFFICIENT_BUFFER = 5,
		FYUU_ERROR_IO = 6,
		FYUU_ERROR_OPERATION_FAILED = 7,
		FYUU_ERROR_OUT_OF_MEMORY = 8
	} Fyuu_Result;

	typedef struct Fyuu_UUID {
		uint8_t bytes[16];
	} Fyuu_UUID;

	typedef struct Fyuu_StringView {
		char const* data;
		size_t size;
	} Fyuu_StringView;

	typedef struct Fyuu_Vec3 {
		float x;
		float y;
		float z;
	} Fyuu_Vec3;

	typedef struct Fyuu_Quat {
		float x;
		float y;
		float z;
		float w;
	} Fyuu_Quat;

	typedef struct Fyuu_Transform {
		Fyuu_Vec3 translation;
		Fyuu_Quat rotation;
		Fyuu_Vec3 scale;
	} Fyuu_Transform;

#if defined(__cplusplus)
}
#endif // defined(__cplusplus)
