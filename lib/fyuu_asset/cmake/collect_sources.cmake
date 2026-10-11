function(collect_sources base_dir group_name out_var)
	file(GLOB_RECURSE collected CONFIGURE_DEPENDS
		"${base_dir}/*.c"
		"${base_dir}/*.cc"
		"${base_dir}/*.cpp"
		"${base_dir}/*.cxx"
	)

	foreach(source_file IN LISTS collected)
		file(RELATIVE_PATH relative_path "${base_dir}" "${source_file}")
		get_filename_component(directory_path "${relative_path}" DIRECTORY)
		if(group_name AND directory_path)
			source_group("Source\\${group_name}\\${directory_path}" FILES "${source_file}")
		elseif(group_name)
			source_group("Source\\${group_name}" FILES "${source_file}")
		elseif(directory_path)
			source_group("Source\\${directory_path}" FILES "${source_file}")
		else()
			source_group("Source" FILES "${source_file}")
		endif()
	endforeach()

	set(${out_var} ${collected} PARENT_SCOPE)
endfunction()
