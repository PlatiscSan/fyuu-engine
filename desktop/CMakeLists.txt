cmake_minimum_required(VERSION 4.2.3)

project(FyuuDesktop LANGUAGES CXX)

include(${PROJECT_ROOT_DIR}/cmake/disable_rtti.cmake)
include(${PROJECT_ROOT_DIR}/cmake/collect_cxx_modules.cmake)

find_package(SDL3 CONFIG REQUIRED)

add_library(${PROJECT_NAME})

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
	PRIVATE FILE_SET internal_modules TYPE CXX_MODULES
		BASE_DIRS
			${CMAKE_CURRENT_SOURCE_DIR}/module
		FILES
			${internal_modules}
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

target_link_libraries(${PROJECT_NAME}
	PUBLIC
		FyuuEngine
	PRIVATE
		SDL3::SDL3
)

set_target_properties(${PROJECT_NAME}
	PROPERTIES
		WINDOWS_EXPORT_ALL_SYMBOLS ON
)

disable_rtti(${PROJECT_NAME})
