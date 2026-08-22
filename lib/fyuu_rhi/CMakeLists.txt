cmake_minimum_required(VERSION 3.28)
project(FyuuRHI VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Boost
find_package(Boost REQUIRED COMPONENTS locale)
find_package(Boost REQUIRED COMPONENTS system)
find_package(Boost REQUIRED COMPONENTS uuid)
find_package(Boost REQUIRED COMPONENTS container)
find_package(Boost REQUIRED COMPONENTS preprocessor)
find_package(Boost REQUIRED COMPONENTS scope)
find_package(Boost REQUIRED COMPONENTS any)
find_package(Boost REQUIRED COMPONENTS asio)
find_package(Boost REQUIRED COMPONENTS lockfree)
find_package(Boost REQUIRED COMPONENTS pool)
find_package(Boost REQUIRED COMPONENTS hash2)

# parallel programming
find_package(PkgConfig REQUIRED)
pkg_check_modules(HWLOC REQUIRED IMPORTED_TARGET hwloc)
find_package(TBB CONFIG REQUIRED)

# Find Vulkan package and check version
find_package(Vulkan REQUIRED)
if(Vulkan_VERSION VERSION_LESS "1.3.256")
	message(FATAL_ERROR 
		"Minimum required Vulkan version for C++ modules is 1.3.256. "
		"Found ${Vulkan_VERSION}."
	)
endif()

# Vulkan memory allocator
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

# webgpu Dawn
find_package(Dawn CONFIG REQUIRED)

# shader compiler
find_package(spirv_cross_core CONFIG REQUIRED)
find_package(spirv_cross_glsl CONFIG REQUIRED)
find_package(spirv_cross_hlsl CONFIG REQUIRED)
find_package(spirv_cross_msl CONFIG REQUIRED)
find_package(spirv_cross_reflect CONFIG REQUIRED)
find_package(spirv_cross_util CONFIG REQUIRED)
find_package(spirv_cross_cpp CONFIG REQUIRED)
find_package(spirv_cross_c CONFIG REQUIRED)
find_package(glslang CONFIG REQUIRED)
find_package(directx-dxc CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)

# serialization
find_package(nlohmann_json CONFIG REQUIRED)

# compile-time data structure
find_package(frozen CONFIG REQUIRED)

if(WIN32)
	# DirectX
	find_package(directx-headers CONFIG REQUIRED)
	find_package(directxtk12 CONFIG REQUIRED)
	find_package(D3D12MemoryAllocator CONFIG REQUIRED)

	# wil
	find_package(wil CONFIG REQUIRED)
endif(WIN32)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/collect_sources.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/collect_cxx_modules.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/disable_rtti.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/collect_headers.cmake)
include(CheckCXXSourceCompiles)

check_cxx_source_compiles(
	"#include <version>
	#if !defined(__clang__) || !defined(_MSVC_STL_VERSION)
	#error Vulkan module is available for this standard-library configuration
	#endif
	int main() { return 0; }"
	FYUU_RHI_USE_VULKAN_HEADER
)

add_library(${PROJECT_NAME} STATIC)

collect_sources(${CMAKE_CURRENT_SOURCE_DIR}/src ""
	sources
)

if(WIN32)
	list(FILTER sources EXCLUDE REGEX ".*glad_glx\\.c$|.*glad_egl\\.c$|.*glad_x11\\.c$")
elseif(ANDROID)
	list(FILTER sources EXCLUDE REGEX ".*glad_glx\\.c$|.*glad_wgl\\.c$")
elseif(APPLE)
	list(FILTER sources EXCLUDE REGEX ".*glad_glx\\.c$|.*glad_egl\\.c$|.*glad_wgl\\.c$")
elseif(UNIX)
	list(FILTER sources EXCLUDE REGEX ".*glad_wgl\\.c$")
endif()

collect_cxx_modules(
	${CMAKE_CURRENT_SOURCE_DIR}/module
	""
	interface_modules
	internal_modules
	module_implementation_units
)

collect_headers(${CMAKE_CURRENT_SOURCE_DIR}/include/ ""
	public_headers
)

target_include_directories(${PROJECT_NAME} 
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(APPLE)
	target_include_directories(
		${PROJECT_NAME}
		PRIVATE
			${PROJECT_SOURCE_DIR}/../dependency/metal-cpp
	)
endif()
	
target_sources(${PROJECT_NAME}
	PUBLIC FILE_SET CXX_MODULES FILES 
		${interface_modules}
	PUBLIC FILE_SET internal_modules TYPE CXX_MODULES FILES
		${internal_modules}
	PRIVATE 
		${public_headers}
	PRIVATE
		${sources}
		${module_implementation_units}
)

target_compile_definitions(${PROJECT_NAME} 
	PRIVATE
		VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
		PIX_ENABLED
		#BOOST_USE_MODULES
		__STDC_WANT_LIB_EXT1__
)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
		plastic
		#-- Boost
		Boost::system
		Boost::locale
		Boost::uuid
		Boost::container
		Boost::preprocessor
		Boost::scope
		Boost::any
		Boost::asio
		Boost::lockfree
		Boost::pool
		Boost::hash2
		# parallel programming
		TBB::tbb TBB::tbbmalloc TBB::tbbbind_2_5 TBB::tbbmalloc_proxy
		#-- Vulkan
		Vulkan::Vulkan
		GPUOpen::VulkanMemoryAllocator
		#-- webgpu Dawn
		dawn::webgpu_dawn
		#-- shader compiler
		spirv-cross-core
		spirv-cross-glsl
		spirv-cross-hlsl
		spirv-cross-msl
		spirv-cross-reflect
		spirv-cross-util
		spirv-cross-cpp
		slang::gfx
		slang::slang
		#-- serialization
		nlohmann_json::nlohmann_json
		# compile-time data structure
		frozen::frozen frozen::frozen-headers
)

if(NOT FYUU_RHI_USE_VULKAN_HEADER)
	target_link_libraries(${PROJECT_NAME} PRIVATE VulkanHppModule)
endif()

if(WIN32)
	target_compile_definitions(
		${PROJECT_NAME}
		PRIVATE
			NOMINMAX
			VK_USE_PLATFORM_WIN32_KHR
	)

	target_link_libraries(${PROJECT_NAME}
		PUBLIC			
			WIL::WIL
			Microsoft::DirectX-Headers
			Microsoft::DirectXTK12
			GPUOpen::D3D12MemoryAllocator

			WinPixEventRuntime


			Opengl32.lib
	)
elseif(UNIX AND NOT ANDROID AND NOT APPLE)
	find_package(OpenGL REQUIRED COMPONENTS OpenGL EGL)
	find_package(X11 REQUIRED)
	pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
	target_link_libraries(
		${PROJECT_NAME}
		PRIVATE
			OpenGL::OpenGL
			OpenGL::EGL
			X11::X11
			PkgConfig::WAYLAND_CLIENT
	)
elseif(ANDROID)
	target_link_libraries(
		${PROJECT_NAME}
		PRIVATE
			EGL
			GLESv3
	)
elseif(APPLE)
	target_link_libraries(
		${PROJECT_NAME}
		PRIVATE
			"-framework Foundation"
			"-framework QuartzCore"
			"-framework Metal"
	)
endif()

disable_rtti(${PROJECT_NAME})

if(BUILD_TESTING)
	add_subdirectory(test/hello_triangle)
endif()
