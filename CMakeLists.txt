# CMakeList.txt : Top-level CMake project file, do global configuration
# and include sub-projects here.
#
cmake_minimum_required(VERSION 4.2.3)

# # Set experimental flag to enable `import std` support from CMake.
# # This must be enabled before C++ language support.
# set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
# 	# This specific value changes as experimental support evolves. See
# 	# `Help/dev/experimental.rst` in the CMake source corresponding to
# 	# your CMake build for the exact value to use.
# 	"451f2fe2-a8a2-47c3-bc32-94786d8fc91b")

set(CMAKE_CXX_MODULES ON) 

# Set C++ standard to C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(FyuuEngine)
include(CTest)

option(FYUU_BUILD_STUDIO "Build Fyuu Studio" OFF)
option(FYUU_BUILD_DESKTOP "Build the SDL3 desktop platform adapter" ON)

# set(CMAKE_CXX_MODULE_STD 1)

# Enable generation of compile_commands.json for IDEs and tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)


# Set output directories for binaries and libraries
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Define project directories
set(PROJECT_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(MSVC)
    # Enable Hot Reload for MSVC compilers if supported.
    if (POLICY CMP0141)
        cmake_policy(SET CMP0141 NEW)
        set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
    endif()

    # Set common MSVC compiler options
    add_compile_options(
        /Zc:__cplusplus # Enable standard __cplusplus macro
        /Zc:preprocessor # Enable standard preprocessor behavior
        /permissive- # Enable standards-compliant mode
        /W4 # Enable high warning level
        /wd4100 # Unreferenced formal parameter
        /wd4324 # Structure was padded due to alignment specifier
        /wd4201 # Nonstandard extension used: nameless struct/union
        /wd4514 # Unreferenced inline function has been removed
        /wd4710 # Function not inlined
        /wd4711 # Function selected for automatic inline expansion
        /bigobj # Enable large object files
        /utf-8 # Set source and execution character set to UTF-8
    )


else()
    add_compile_options(
        -Wall # Enable all warnings
        -Wextra # Enable extra warnings
        -Wpedantic # Enable pedantic warnings
        -Wno-unused-parameter # Suppress unused parameter warnings
        -Wno-unused-variable # Suppress unused variable warnings
        -Wno-unused-function # Suppress unused function warnings
        -Wno-sign-compare # Suppress signed/unsigned comparison warnings
    )


endif()

# Enable position independent code for all targets
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_subdirectory(lib/plastic)
add_subdirectory(lib/vulkan_hpp)
add_subdirectory(lib/PixEvents-cmake)
add_subdirectory(lib/fyuu_rhi)
add_subdirectory(lib/fyuu_asset)

add_subdirectory(runtime)

if(FYUU_BUILD_DESKTOP)
    add_subdirectory(desktop)
endif()

if(FYUU_BUILD_STUDIO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/studio/CMakeLists.txt")
    add_subdirectory(studio)
endif()

if(BUILD_TESTING)
    add_subdirectory("test/fyuu_asset")
endif()
