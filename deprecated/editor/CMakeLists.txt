project(FyuuEditor)

include(${PROJECT_ROOT_DIR}/cmake/collect_sources.cmake)
include(${PROJECT_ROOT_DIR}/cmake/collect_cxx_modules.cmake)

add_executable(${PROJECT_NAME})

collect_sources(${CMAKE_CURRENT_SOURCE_DIR}/src ""
    editor_sources
)

collect_cxx_modules(${CMAKE_CURRENT_SOURCE_DIR}/src ""
    editor_modules
)

target_sources(${PROJECT_NAME}
    PRIVATE
        ${editor_sources}
    PRIVATE FILE_SET CXX_MODULES
        FILES
            ${editor_modules}
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        FyuuEngine
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
            BUILD_STATIC_LIBS
    )
endif()

disable_rtti(${PROJECT_NAME})
