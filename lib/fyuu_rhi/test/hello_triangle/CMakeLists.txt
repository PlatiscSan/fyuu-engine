project(HelloTriangle LANGUAGES CXX)

add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE FyuuRHI)

if(UNIX AND NOT APPLE AND NOT ANDROID)
	find_package(X11 REQUIRED)
	target_link_libraries(${PROJECT_NAME} PRIVATE X11::X11)
endif()
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

function(add_hello_triangle_test backend)
	add_test(
		NAME HelloTriangle.${backend}
		COMMAND HelloTriangle ${backend} --test
	)
	set_tests_properties(HelloTriangle.${backend} PROPERTIES TIMEOUT 60)
endfunction()

if(WIN32)
	add_hello_triangle_test(d3d12)
endif()
if(APPLE)
	add_hello_triangle_test(metal)
else()
	add_hello_triangle_test(vulkan)
	add_hello_triangle_test(opengl)
endif()
add_hello_triangle_test(webgpu)
