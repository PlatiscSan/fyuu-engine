cmake_minimum_required(VERSION 4.2.3)
project(FyuuMath VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/disable_rtti.cmake)

add_library(FyuuMath STATIC)
target_sources(FyuuMath PRIVATE module/internal/simd.cpp)
add_library(Fyuu::Math ALIAS FyuuMath)
target_compile_features(FyuuMath PUBLIC cxx_std_23)
option(FYUU_MATH_ENABLE_SIMD "Enable internal SIMD kernels on supported targets" ON)
target_compile_definitions(FyuuMath PRIVATE FYUU_MATH_ENABLE_SIMD=$<BOOL:${FYUU_MATH_ENABLE_SIMD}>)
set_target_properties(FyuuMath PROPERTIES CXX_EXTENSIONS OFF)
target_sources(FyuuMath
    PUBLIC FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/module"
        FILES
            module/fyuu_math.cppm
            module/traits.cppm
            module/concepts.cppm
            module/operations.cppm
            module/transform.cppm
            module/projection.cppm
)

disable_rtti(FyuuMath)
# Module consumers must use the same RTTI setting as the module producer.
target_compile_options(FyuuMath PUBLIC
    $<$<CXX_COMPILER_ID:MSVC>:/GR->
    $<$<CXX_COMPILER_ID:Clang,GNU>:-fno-rtti>)

include(CTest)
if(BUILD_TESTING)
    add_executable(fyuu_math_contracts test/contracts.cpp)
    target_link_libraries(fyuu_math_contracts PRIVATE FyuuMath)
    add_test(NAME fyuu_math.contracts COMMAND fyuu_math_contracts)
    add_executable(fyuu_math_simd_contracts test/simd_contracts.cpp)
    target_link_libraries(fyuu_math_simd_contracts PRIVATE FyuuMath)
    add_test(NAME fyuu_math.simd_contracts COMMAND fyuu_math_simd_contracts)
    add_executable(fyuu_math_builtin_adapters test/builtin_adapters.cpp)
    target_link_libraries(fyuu_math_builtin_adapters PRIVATE FyuuMath)
    add_test(NAME fyuu_math.builtin_adapters COMMAND fyuu_math_builtin_adapters)
    add_executable(fyuu_math_dynamic_span test/dynamic_span.cpp)
    target_link_libraries(fyuu_math_dynamic_span PRIVATE FyuuMath)
    add_test(NAME fyuu_math.dynamic_span COMMAND fyuu_math_dynamic_span)
    # Kernel benchmarks: they print ns/op and assert their results, so ctest runs them.
    add_executable(fyuu_math_matrix_kernels test/matrix_kernels.cpp)
    target_link_libraries(fyuu_math_matrix_kernels PRIVATE FyuuMath)
    add_test(NAME fyuu_math.matrix_kernels COMMAND fyuu_math_matrix_kernels)
endif()

option(FYUU_MATH_WITH_GLM "Build the optional GLM adapter" OFF)
option(FYUU_MATH_WITH_EIGEN "Build the optional Eigen adapter" OFF)
if(FYUU_MATH_WITH_GLM)
    find_path(FYUU_MATH_GLM_INCLUDE_DIR glm/glm.hpp REQUIRED)
    add_library(FyuuMathGLM STATIC)
    add_library(Fyuu::MathGLM ALIAS FyuuMathGLM)
    target_sources(FyuuMathGLM PUBLIC FILE_SET CXX_MODULES FILES adapter/glm.cppm)
    target_include_directories(FyuuMathGLM SYSTEM PUBLIC "${FYUU_MATH_GLM_INCLUDE_DIR}")
    target_link_libraries(FyuuMathGLM PUBLIC FyuuMath)
endif()
if(FYUU_MATH_WITH_EIGEN)
    find_path(FYUU_MATH_EIGEN_INCLUDE_DIR Eigen/Core PATH_SUFFIXES eigen3 REQUIRED)
    add_library(FyuuMathEigen STATIC)
    add_library(Fyuu::MathEigen ALIAS FyuuMathEigen)
    target_sources(FyuuMathEigen PUBLIC FILE_SET CXX_MODULES FILES adapter/eigen.cppm)
    target_include_directories(FyuuMathEigen SYSTEM PUBLIC "${FYUU_MATH_EIGEN_INCLUDE_DIR}")
    target_link_libraries(FyuuMathEigen PUBLIC FyuuMath)
    if(BUILD_TESTING)
        add_executable(fyuu_math_strided_eigen test/strided_eigen.cpp)
        target_link_libraries(fyuu_math_strided_eigen PRIVATE Fyuu::MathEigen)
        add_test(NAME fyuu_math.strided_eigen COMMAND fyuu_math_strided_eigen)
        add_executable(fyuu_math_backend_batch test/backend_batch.cpp)
        target_link_libraries(fyuu_math_backend_batch PRIVATE Fyuu::MathEigen)
        add_test(NAME fyuu_math.backend_batch COMMAND fyuu_math_backend_batch)
    endif()
endif()

if(BUILD_TESTING AND FYUU_MATH_WITH_GLM AND FYUU_MATH_WITH_EIGEN)
    add_executable(fyuu_math_backends test/backends.cpp)
    target_link_libraries(fyuu_math_backends PRIVATE Fyuu::MathGLM Fyuu::MathEigen)
    add_test(NAME fyuu_math.backends COMMAND fyuu_math_backends)
endif()

