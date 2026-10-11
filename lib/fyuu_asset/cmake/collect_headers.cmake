function(collect_headers base_dir group_name out_var)
	file(GLOB_RECURSE collected CONFIGURE_DEPENDS
		"${base_dir}/*.h"
		"${base_dir}/*.hh"
		"${base_dir}/*.hpp"
		"${base_dir}/*.hxx"
		"${base_dir}/*.inc"
		"${base_dir}/*.inl"
		"${base_dir}/*.ipp"
		"${base_dir}/*.tpp"
	)

	if(collected)
		if(group_name)
			source_group("Header\\${group_name}" FILES ${collected})
		else()
			source_group("Header" FILES ${collected})
		endif()
	endif()

	set(${out_var} ${collected} PARENT_SCOPE)
endfunction()
