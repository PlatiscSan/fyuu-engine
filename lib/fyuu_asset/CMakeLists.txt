cmake_minimum_required(VERSION 4.2.3)

project(FyuuAsset VERSION 1.0.0 LANGUAGES CXX)

option(FYUU_ASSET_BUILD_DOCUMENTATION "Build FyuuAsset API documentation with Doxygen" OFF)

find_package(Boost REQUIRED COMPONENTS uuid)
find_package(Boost REQUIRED COMPONENTS locale)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(minizip CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(Stb REQUIRED)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/collect_sources.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/collect_cxx_modules.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/collect_headers.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/disable_rtti.cmake)

add_library(${PROJECT_NAME} STATIC)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

collect_sources(${CMAKE_CURRENT_SOURCE_DIR}/src "" sources)
collect_cxx_modules(
	${CMAKE_CURRENT_SOURCE_DIR}/module
	""
	interface_modules
	internal_modules
	implementation_units
)
collect_headers(${CMAKE_CURRENT_SOURCE_DIR}/include "" public_headers)

target_sources(${PROJECT_NAME}
	PUBLIC
		FILE_SET CXX_MODULES FILES ${interface_modules}
	PRIVATE
		FILE_SET internal_modules TYPE CXX_MODULES FILES ${internal_modules}
	PRIVATE
		${sources}
		${public_headers}
		${implementation_units}
)

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(
	${PROJECT_NAME}
	SYSTEM PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/external/MMDLoader
		${Stb_INCLUDE_DIR}
)

disable_rtti(${PROJECT_NAME})

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
		plastic
		Boost::uuid
		Boost::locale
		nlohmann_json::nlohmann_json
		$<IF:$<TARGET_EXISTS:MINIZIP::minizip>,MINIZIP::minizip,MINIZIP::minizipstatic>
		assimp::assimp
)

if(BUILD_TESTING)
	add_subdirectory(test)
endif()

if(FYUU_ASSET_BUILD_DOCUMENTATION)
	add_subdirectory(doc)
endif()
