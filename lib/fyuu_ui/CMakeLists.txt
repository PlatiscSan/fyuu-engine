cmake_minimum_required(VERSION 4.2.3)

project(FyuuUI VERSION 1.0.0 LANGUAGES CXX)

include(${PROJECT_ROOT_DIR}/cmake/collect_cxx_modules.cmake)
include(${PROJECT_ROOT_DIR}/cmake/disable_rtti.cmake)

add_library(${PROJECT_NAME} STATIC)

collect_cxx_modules(
	${CMAKE_CURRENT_SOURCE_DIR}/module
	""
	public_modules
	internal_modules
)

target_sources(${PROJECT_NAME}
	PUBLIC FILE_SET public_modules TYPE CXX_MODULES
		BASE_DIRS
			${CMAKE_CURRENT_SOURCE_DIR}/module
		FILES
			${public_modules}
)

if(internal_modules)
	target_sources(${PROJECT_NAME}
		PRIVATE FILE_SET internal_modules TYPE CXX_MODULES
			BASE_DIRS
				${CMAKE_CURRENT_SOURCE_DIR}/module
			FILES
				${internal_modules}
	)
endif()

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

disable_rtti(${PROJECT_NAME})
