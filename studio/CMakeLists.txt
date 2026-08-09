cmake_minimum_required(VERSION 4.2.3)

project(FyuuStudio LANGUAGES CXX)

include(${PROJECT_ROOT_DIR}/cmake/disable_rtti.cmake)

if(NOT TARGET FyuuDesktop)
	message(FATAL_ERROR "FyuuStudio requires FYUU_BUILD_DESKTOP=ON")
endif()

add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

target_link_libraries(${PROJECT_NAME}
	PRIVATE
		FyuuDesktop
)

disable_rtti(${PROJECT_NAME})
