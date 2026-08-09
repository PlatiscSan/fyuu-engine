#pragma once
#include "api_macro.h"
#include "fyuu_types.h"

#if defined(__cplusplus)
#include <cstdint>
#else
#include <stdint.h>
#endif // defined(__cplusplus)

#if defined(__cplusplus)
extern "C" {
#endif // defined(__cplusplus)

	typedef struct Fyuu_Version {
		uint8_t variant;
		uint8_t major;
		uint8_t minor;
		uint8_t patch;
	} Fyuu_Version;

	typedef struct Fyuu_App {

		char const* description;
		char const* name;
		char const* title;
		
		uint32_t surface_width;
		uint32_t surface_height;

		Fyuu_Version version;
		float font_size;

		void* user_data;
		Fyuu_Bool request_stop;

		void(*Init)(Fyuu_App* self) NOEXCEPT;
		void(*Tick)(Fyuu_App* self) NOEXCEPT;
		Fyuu_Bool(*CloseRequested)(Fyuu_App* self) NOEXCEPT;
		void(*Shutdown)(Fyuu_App* self) NOEXCEPT;

	} Fyuu_App;

	LIB_API int LIB_CALL Fyuu_Run(int argc, char** argv, Fyuu_App* app) NOEXCEPT;

#if defined(__cplusplus)
}
#endif // defined(__cplusplus)
