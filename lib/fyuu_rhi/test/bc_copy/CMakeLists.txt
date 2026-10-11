add_executable(BCCopyProbe main.cpp)
target_link_libraries(BCCopyProbe PRIVATE FyuuRHI)
disable_rtti(BCCopyProbe)
foreach(backend IN ITEMS vulkan opengl webgpu)
	add_test(NAME BCCopyProbe.${backend} COMMAND BCCopyProbe ${backend})
	set_tests_properties(BCCopyProbe.${backend} PROPERTIES TIMEOUT 120)
endforeach()
add_test(NAME BCCopyProbe.webgpu.large COMMAND BCCopyProbe webgpu large)
set_tests_properties(BCCopyProbe.webgpu.large PROPERTIES TIMEOUT 120)
add_test(NAME BCCopyProbe.webgpu.aligned COMMAND BCCopyProbe webgpu aligned)
set_tests_properties(BCCopyProbe.webgpu.aligned PROPERTIES TIMEOUT 120)
if(WIN32)
	foreach(mode IN ITEMS compare compare.aligned compare.palette compare.alpha compare.palette.float compare.alpha.float compare.channels compare.bptc compare.bptc.odd compare.palette.aniso compare.channels.aniso compare.bptc.aniso decode)
		add_test(NAME BCCopyProbe.webgpu.${mode} COMMAND BCCopyProbe webgpu ${mode})
		set_tests_properties(BCCopyProbe.webgpu.${mode} PROPERTIES
			TIMEOUT 120
			ENVIRONMENT "FYUU_RHI_WEBGPU_DISABLE_BC=0"
		)
	endforeach()
	foreach(mode IN ITEMS software software.aligned software.large software.channels software.bptc)
		add_test(NAME BCCopyProbe.webgpu.${mode} COMMAND BCCopyProbe webgpu ${mode})
		set_tests_properties(BCCopyProbe.webgpu.${mode} PROPERTIES
			TIMEOUT 120
			ENVIRONMENT "FYUU_RHI_WEBGPU_DISABLE_BC=1"
		)
	endforeach()
	foreach(mode IN ITEMS compare.palette.float compare.alpha.float compare.palette.aniso compare.channels compare.channels.aniso compare.bptc compare.bptc.odd compare.bptc.aniso)
		add_test(NAME BCCopyProbe.webgpu.intel.${mode} COMMAND BCCopyProbe webgpu ${mode} 0x8086)
		set_tests_properties(BCCopyProbe.webgpu.intel.${mode} PROPERTIES
			TIMEOUT 120
			SKIP_RETURN_CODE 77
			ENVIRONMENT "FYUU_RHI_WEBGPU_DISABLE_BC=0"
		)
	endforeach()
	foreach(mode IN ITEMS software software.channels software.bptc)
		add_test(NAME BCCopyProbe.webgpu.intel.automatic.${mode} COMMAND BCCopyProbe webgpu ${mode} 0x8086)
		set_tests_properties(BCCopyProbe.webgpu.intel.automatic.${mode} PROPERTIES
			TIMEOUT 120
			SKIP_RETURN_CODE 77
			ENVIRONMENT "FYUU_RHI_WEBGPU_DISABLE_BC=1"
		)
	endforeach()
endif()
