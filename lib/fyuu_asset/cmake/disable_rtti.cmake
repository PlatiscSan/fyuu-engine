function(disable_rtti target_name)
	set_target_properties(${target_name} PROPERTIES CXX_RTTI OFF)

	if(MSVC)
		target_compile_options(${target_name} PUBLIC /GR-)
	elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
		target_compile_options(${target_name} PUBLIC -fno-rtti)
	endif()
endfunction()
