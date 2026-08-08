#pragma once
	
#if defined(_WIN32) || defined(__CYGWIN__)
	// Windows / Cygwin
	#if defined(__GNUC__)
		// MinGW / Cygwin GCC
		#define DLL_EXPORT __attribute__((dllexport))
		#define DLL_IMPORT __attribute__((dllimport))
	#else
		// MSVC
		#define DLL_EXPORT __declspec(dllexport)
		#define DLL_IMPORT __declspec(dllimport)
	#endif
#else
	// Linux / macOS
	#if __GNUC__ >= 4 || defined(__clang__)
		#define DLL_EXPORT __attribute__((visibility("default")))
		#define DLL_IMPORT
	#else
		#define DLL_EXPORT
		#define DLL_IMPORT
	#endif
#endif

#ifdef BUILD_SHARED_LIBS
	#define LIB_API DLL_EXPORT
#else
	#define LIB_API
#endif

#if defined(_WIN32) && !defined(__GNUC__)
	 #define LIB_CALL __cdecl
#else
	 #define LIB_CALL
#endif

#if defined(__cplusplus)
	#define NOEXCEPT noexcept(true)
#else
	#define NOEXCEPT
#endif
