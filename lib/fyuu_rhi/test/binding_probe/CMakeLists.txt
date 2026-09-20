project(BindingProbe LANGUAGES CXX)

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

# One probe per backend and declaration shape. The probe creates the objects a resource group is
# built from and checks which BindingValue kinds each reflected slot accepts, which is the one
# contract all five backends have to agree on:
#   combined  one Sampler2D at (t1, space1), the shape the engine's Vulkan path uses. The group has
#             to carry both halves of the pair and must reject a value carrying only one, a buffer,
#             an undeclared slot, a duplicate, an unbound slot, and an undeclared space.
#   separate  Texture2D at (t1, space1) plus SamplerState at (s2, space1), the shape the engine's
#             D3D12 and WebGPU paths use. A target whose reflection folds the pair into one
#             combined binding is reported rather than failed, and the mismatch cases then run
#             only where the pair is accepted.
# Both modes also create a sampler asking for anisotropy, which fails if the backend logs the
# VUID that a device without samplerAnisotropy raises.
set(binding_probe_modes combined separate)

function(add_binding_probe_test backend)
	foreach(mode IN LISTS binding_probe_modes)
		add_test(NAME BindingProbe.${backend}.${mode} COMMAND BindingProbe ${backend} ${mode})
		set_tests_properties(BindingProbe.${backend}.${mode} PROPERTIES TIMEOUT 120)
	endforeach()
endfunction()

if(WIN32)
	add_binding_probe_test(d3d12)
endif()
if(APPLE)
	add_binding_probe_test(metal)
else()
	add_binding_probe_test(vulkan)
	add_binding_probe_test(opengl)
endif()
add_binding_probe_test(webgpu)
