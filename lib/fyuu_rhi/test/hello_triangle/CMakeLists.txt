project(HelloTriangle LANGUAGES CXX)

add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE FyuuRHI)

if(UNIX AND NOT APPLE AND NOT ANDROID)
	find_package(X11 REQUIRED)
	target_link_libraries(${PROJECT_NAME} PRIVATE X11::X11)
endif()
disable_rtti(${PROJECT_NAME})

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
