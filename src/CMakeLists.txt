# src/CMakeLists.txt
cmake_minimum_required(VERSION 4.2.3)
set(CMAKE_CXX_MODULES ON) 

project(FyuuEngine)

include(${PROJECT_ROOT_DIR}/cmake/disable_rtti.cmake)
include(${PROJECT_ROOT_DIR}/cmake/collect_cxx_modules.cmake)
include(${PROJECT_ROOT_DIR}/cmake/collect_sources.cmake)
include(${PROJECT_ROOT_DIR}/cmake/collect_headers.cmake)

# Boost
find_package(Boost REQUIRED COMPONENTS uuid)
find_package(Boost REQUIRED COMPONENTS thread)
find_package(Boost REQUIRED COMPONENTS describe)
find_package(Boost REQUIRED COMPONENTS gil)

if(Boost_FOUND)
    add_definitions(-DBOOST_UUID_RANDOM_PROVIDER_FORCE_POSIX)
else()
    message(FATAL_ERROR "Boost not found!")
endif()

# command line parser
find_package(CLI11 CONFIG REQUIRED)

# serialization
find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# parallel programming
find_package(PkgConfig REQUIRED)
pkg_check_modules(HWLOC REQUIRED IMPORTED_TARGET hwloc)
find_package(TBB CONFIG REQUIRED)

# log
find_package(spdlog CONFIG REQUIRED)

# graphics libraries
find_package(SDL3 CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

# physics
find_package(Jolt CONFIG REQUIRED)

if(BUILD_SHARED_LIBS)
	add_library(${PROJECT_NAME} SHARED)
else()
	add_library(${PROJECT_NAME} STATIC)
	target_compile_definitions(${PROJECT_NAME}
		PRIVATE
			BUILD_STATIC_LIBS
	)
endif()

collect_sources(${ENGINE_SOURCE_DIR} ""
	sources
)

collect_cxx_modules(${ENGINE_SOURCE_DIR} ""
	modules 
	module_impls
)

target_sources(${PROJECT_NAME}
	PUBLIC 
		${PUBLIC_HEADERS}
)

target_sources(${PROJECT_NAME}
	PUBLIC FILE_SET CXX_MODULES
		BASE_DIRS
			${PROJECT_ROOT_DIR}/module
			${CMAKE_CURRENT_SOURCE_DIR}
		FILES 
			${MODULE_INTERFACES}
			${modules}
	PRIVATE
		${module_impls}
		${sources}
)

target_include_directories(${PROJECT_NAME}
	PUBLIC
		${PROJECT_ROOT_DIR}/include
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/internal
)

target_compile_features(${PROJECT_NAME}
	PRIVATE   cxx_std_23
	INTERFACE cxx_std_20
)
	
target_link_libraries(${PROJECT_NAME}
	PRIVATE
		plastic
		FyuuRHI

		# Boost
		Boost::uuid
		Boost::thread
		Boost::describe
		Boost::gil
		# command line parser
		CLI11::CLI11

		# graphics
		SDL3::SDL3
		imgui::imgui

		# physics
		Jolt::Jolt

		# log
		spdlog::spdlog

		# serialization
		yaml-cpp::yaml-cpp
		nlohmann_json::nlohmann_json

		# parallel programming
		TBB::tbb TBB::tbbmalloc TBB::tbbbind_2_5 TBB::tbbmalloc_proxy

)

disable_rtti(${PROJECT_NAME})