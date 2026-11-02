include(${CMAKE_CURRENT_LIST_DIR}/collect_cxx_modules.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/disable_rtti.cmake)

collect_cxx_modules(
	${PROJECT_SOURCE_DIR}/module
	""
	interface_modules
	internal_modules
	module_implementation_units
)

foreach(source_list IN ITEMS internal_modules module_implementation_units)
	list(FILTER ${source_list} EXCLUDE REGEX "/(d3d12|vulkan|opengl|metal)(/|\\.cpp$)")
	list(FILTER ${source_list} EXCLUDE REGEX "/instance/(wgl|glx|egl|gl_common)\\.cpp$")
endforeach()

find_package(Boost CONFIG REQUIRED COMPONENTS hash2)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(frozen CONFIG REQUIRED)
find_package(slang CONFIG REQUIRED)

if(NOT TARGET plastic)
	add_subdirectory(${PROJECT_SOURCE_DIR}/../plastic ${PROJECT_BINARY_DIR}/plastic)
endif()

add_library(${PROJECT_NAME} STATIC)
target_sources(${PROJECT_NAME}
	PUBLIC FILE_SET CXX_MODULES FILES ${interface_modules}
	PUBLIC FILE_SET internal_modules TYPE CXX_MODULES FILES ${internal_modules}
	PRIVATE ${module_implementation_units} ${PROJECT_SOURCE_DIR}/src/parallel_for.cpp
)
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(${PROJECT_NAME}
	PRIVATE plastic Boost::hash2 nlohmann_json::nlohmann_json frozen::frozen-headers slang::slang
)
target_compile_options(${PROJECT_NAME} PUBLIC --use-port=emdawnwebgpu -fexceptions)
target_link_options(${PROJECT_NAME}
	PUBLIC --use-port=emdawnwebgpu -fexceptions -sASYNCIFY=1 -sALLOW_MEMORY_GROWTH=1
)
disable_rtti(${PROJECT_NAME})
