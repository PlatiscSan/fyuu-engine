project(DrawShapes LANGUAGES CXX)

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

# Registers one drawing mode for one backend. Each mode renders offscreen and checks the pixels it
# reads back, so a mode fails when the primitive, the vertex or index path, the attachment state, or
# the sampling it names did not actually take effect:
#   lines            LineList through a row centre: only the rows a one-pixel line can cover may be
#                    lit, and the near end has to be reached
#   strip            TriangleStrip, four vertices forming one quad
#   indexed          The same quad through an index buffer and DrawIndexed with a base vertex of 4,
#                    so a backend that ignores vertex_offset rasterizes placeholders that are
#                    outside the clip volume and lights nothing
#   scissor          A full-target quad clipped by Scissor: nothing past column 64 may be lit
#   depth            Two overlapping quads with a D24S8 attachment and a Less test, near drawn
#                    first, so the overlap stays red only if the farther quad is really rejected
#   depth_only       The same depth test on a depth-only D32Float attachment, which a backend that
#                    hands the API a stencil load/store pair unconditionally cannot create
#   blend            A half-transparent red quad over a blue clear with SourceAlpha blending, so
#                    the result is neither input
#   texture          A 2x2 texture uploaded through a staging buffer and sampled with nearest
#                    filtering: all four texel colours must appear in the four quadrants
#   texture_separate The same texture with separately declared texture and sampler bindings, the
#                    shape D3D12 and WebGPU use in the engine
set(draw_shapes_modes lines strip indexed scissor depth depth_only blend texture texture_separate)

function(add_draw_shapes_test backend)
	# The bare name runs every mode in one process, so a single CTest entry still covers the set.
	add_test(NAME DrawShapes.${backend} COMMAND DrawShapes ${backend})
	set_tests_properties(DrawShapes.${backend} PROPERTIES TIMEOUT 300)
	foreach(mode IN LISTS draw_shapes_modes)
		add_test(NAME DrawShapes.${backend}.${mode} COMMAND DrawShapes ${backend} ${mode})
		set_tests_properties(DrawShapes.${backend}.${mode} PROPERTIES TIMEOUT 120)
	endforeach()
endfunction()

if(WIN32)
	add_draw_shapes_test(d3d12)
endif()
if(APPLE)
	add_draw_shapes_test(metal)
else()
	add_draw_shapes_test(vulkan)
	add_draw_shapes_test(opengl)
endif()
add_draw_shapes_test(webgpu)
