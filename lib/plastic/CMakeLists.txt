cmake_minimum_required(VERSION 3.28)
project(plastic VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_SCAN_FOR_MODULES ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/collect_cxx_modules.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/disable_rtti.cmake)

add_library(${PROJECT_NAME} STATIC)

collect_cxx_modules(
	${CMAKE_CURRENT_SOURCE_DIR}/src/ds
	"data_structures"
	ds_modules
	ds_internal_modules
)

collect_cxx_modules(
	${CMAKE_CURRENT_SOURCE_DIR}/src/concurrency
	"concurrency"
	concurrency_modules
	concurrency_internal_modules
)


target_sources(${PROJECT_NAME}
    PUBLIC FILE_SET CXX_MODULES FILES 
        ${ds_modules}
        ${concurrency_modules}
)

if(ds_internal_modules)
	target_sources(${PROJECT_NAME}
		PRIVATE FILE_SET ds_internal_modules TYPE CXX_MODULES FILES
			${ds_internal_modules}
	)
endif()

if(concurrency_internal_modules)
	target_sources(${PROJECT_NAME}
		PRIVATE FILE_SET concurrency_internal_modules TYPE CXX_MODULES FILES
			${concurrency_internal_modules}
	)
endif()

target_include_directories(${PROJECT_NAME}
    PUBLIC 
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)


disable_rtti(${PROJECT_NAME})
