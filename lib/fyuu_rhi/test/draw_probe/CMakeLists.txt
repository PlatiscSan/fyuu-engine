project(DrawProbe LANGUAGES CXX)

add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE FyuuRHI)
disable_rtti(${PROJECT_NAME})

if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/dxcompiler.dll"
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/dxil.dll"
			"$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
	)
endif()

# Registers one probe mode for one backend. The bare mode name is the default (single-attribute
# layout), and every extra mode is a pixel-level check of a binding or pipeline-convention path:
#   hello     two-attribute vertex layout, with the fragment stage consuming the interpolated
#             colour, so the vertex attribute locations must survive translation
#   early     resource group and vertex buffer bound before the render scope opens
#   zeroz     gl_Position.z = 0, the near plane of the engine's [0, 1] depth convention, which a
#             clip-space fixup re-mapping the range would clip away
#   pushcolor immediate constants multiplied into the fragment colour, so a backend that drops
#             SetPipelineConstants (an aliased OpenGL binding unit, say) draws the wrong pixel
#   pushcolor_space0
#             the same immediate-constant check with the user uniform buffer in space 0, where
#             D3D12 used to declare the emulated immediate-constant range on top of the user's
#             own CBV register (b0, space0) and fail root signature serialization
set(draw_probe_modes hello early zeroz pushcolor pushcolor_space0)

function(add_draw_probe_test backend)
	add_test(NAME DrawProbe.${backend} COMMAND DrawProbe ${backend})
	set_tests_properties(DrawProbe.${backend} PROPERTIES TIMEOUT 120)
	foreach(mode IN LISTS draw_probe_modes)
		add_test(NAME DrawProbe.${backend}.${mode} COMMAND DrawProbe ${backend} ${mode})
		set_tests_properties(DrawProbe.${backend}.${mode} PROPERTIES TIMEOUT 120)
	endforeach()
endfunction()

if(WIN32)
	add_draw_probe_test(d3d12)
endif()
if(APPLE)
	add_draw_probe_test(metal)
else()
	add_draw_probe_test(vulkan)
	add_draw_probe_test(opengl)
endif()
add_draw_probe_test(webgpu)
