cmake_minimum_required(VERSION 4.2.3)
project(FyuuAsset VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
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
find_package(Boost REQUIRED COMPONENTS bimap)
find_package(Boost REQUIRED COMPONENTS describe)

# parallel programming
find_package(PkgConfig REQUIRED)
pkg_check_modules(HWLOC REQUIRED IMPORTED_TARGET hwloc)
find_package(TBB CONFIG REQUIRED)

# serialization
find_package(yaml-cpp CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/collect_sources.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/collect_cxx_modules.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/disable_rtti.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/collect_headers.cmake)

add_library(${PROJECT_NAME} STATIC)

collect_cxx_modules(
	${CMAKE_CURRENT_SOURCE_DIR}/module
	""
	implementation_modules
	internal_modules
)
collect_headers(${CMAKE_CURRENT_SOURCE_DIR}/include/ "" public_headers)

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Guard each file set: until the asset modules exist the library is built as
# an empty static archive, so empty sets must not produce a configure error.
if(implementation_modules)
    target_sources(${PROJECT_NAME}
        PUBLIC FILE_SET CXX_MODULES FILES ${implementation_modules}
    )
endif()
if(internal_modules)
    target_sources(${PROJECT_NAME}
		PRIVATE FILE_SET internal_modules TYPE CXX_MODULES FILES ${internal_modules}
    )
endif()
if(public_headers)
    target_sources(${PROJECT_NAME} PRIVATE ${public_headers})
endif()

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
		Boost::bimap
		Boost::describe
		# parallel programming
		TBB::tbb TBB::tbbmalloc TBB::tbbbind_2_5 TBB::tbbmalloc_proxy
		#-- serialization
		nlohmann_json::nlohmann_json
        yaml-cpp::yaml-cpp
)

disable_rtti(${PROJECT_NAME})

if(BUILD_TESTING)
	add_subdirectory(test)
endif()
