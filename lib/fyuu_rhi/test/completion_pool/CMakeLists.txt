project(CompletionPool LANGUAGES CXX)

add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE FyuuRHI)

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

# The thread-hygiene case takes wall-clock time by design: it waits out the idle interval that
# lets retired workers go away.
function(add_completion_pool_test backend)
	add_test(
		NAME CompletionPool.${backend}
		COMMAND CompletionPool ${backend}
	)
	set_tests_properties(CompletionPool.${backend} PROPERTIES TIMEOUT 180)
endfunction()

if(WIN32)
	add_completion_pool_test(d3d12)
endif()
if(APPLE)
	add_completion_pool_test(metal)
else()
	add_completion_pool_test(vulkan)
	add_completion_pool_test(opengl)
endif()
add_completion_pool_test(webgpu)
