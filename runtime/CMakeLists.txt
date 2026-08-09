cmake_minimum_required(VERSION 4.2.3)

project(FyuuEngine LANGUAGES CXX)

include(${PROJECT_ROOT_DIR}/cmake/disable_rtti.cmake)
include(${PROJECT_ROOT_DIR}/cmake/collect_cxx_modules.cmake)
include(${PROJECT_ROOT_DIR}/cmake/collect_headers.cmake)

find_package(Python3 QUIET COMPONENTS Interpreter)

set(FYUU_RUNTIME_API_SCHEMA ${PROJECT_ROOT_DIR}/conf/runtime_api.yaml)
set(FYUU_RUNTIME_API_GENERATOR ${PROJECT_ROOT_DIR}/script/generate_runtime_api.py)

if(Python3_Interpreter_FOUND)
	add_custom_target(generate_runtime_api
		COMMAND
			${Python3_EXECUTABLE} ${FYUU_RUNTIME_API_GENERATOR}
		WORKING_DIRECTORY
			${PROJECT_ROOT_DIR}
		DEPENDS
			${FYUU_RUNTIME_API_SCHEMA}
			${FYUU_RUNTIME_API_GENERATOR}
		VERBATIM
	)

	add_custom_target(check_runtime_api
		COMMAND
			${Python3_EXECUTABLE} ${FYUU_RUNTIME_API_GENERATOR} --check
		WORKING_DIRECTORY
			${PROJECT_ROOT_DIR}
		DEPENDS
			${FYUU_RUNTIME_API_SCHEMA}
			${FYUU_RUNTIME_API_GENERATOR}
		VERBATIM
	)
endif()

add_library(${PROJECT_NAME})

collect_cxx_modules(
	${CMAKE_CURRENT_SOURCE_DIR}/module
	""
	public_modules
	internal_modules
)
collect_headers(${PROJECT_ROOT_DIR}/include "" public_headers)

target_sources(${PROJECT_NAME}
	PUBLIC FILE_SET public_headers TYPE HEADERS
		BASE_DIRS
			${PROJECT_ROOT_DIR}/include
		FILES
			${public_headers}
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

target_include_directories(${PROJECT_NAME}
	PUBLIC
		${PROJECT_ROOT_DIR}/include
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

if(BUILD_SHARED_LIBS)
	target_compile_definitions(${PROJECT_NAME}
		PRIVATE
			FYUU_ENGINE_EXPORTS
	)
else()
	target_compile_definitions(${PROJECT_NAME}
		PUBLIC
			FYUU_STATIC
	)
endif()

disable_rtti(${PROJECT_NAME})
