cmake_minimum_required(VERSION 4.2.3)

project(FyuuStudio LANGUAGES CXX)

include(${PROJECT_ROOT_DIR}/cmake/disable_rtti.cmake)
include(${PROJECT_ROOT_DIR}/cmake/collect_cxx_modules.cmake)

if(NOT TARGET FyuuDesktop)
	message(FATAL_ERROR "FyuuStudio requires FYUU_BUILD_DESKTOP=ON")
endif()

add_executable(${PROJECT_NAME})

find_package(Freetype REQUIRED)

collect_cxx_modules(
	${CMAKE_CURRENT_SOURCE_DIR}/module
	""
	public_modules
	internal_modules
)

target_sources(${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
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

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
		FyuuAsset
		FyuuDesktop
		FyuuRHI
		FyuuUI
		Freetype::Freetype
)

disable_rtti(${PROJECT_NAME})

# Dawn's D3D12 backend loads dxcompiler.dll / dxil.dll at runtime (LoadLibrary),
# so applocal does not copy them; copy them next to the executable on Windows.
if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/dxcompiler.dll"
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/dxil.dll"
			"$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
	)
endif()
