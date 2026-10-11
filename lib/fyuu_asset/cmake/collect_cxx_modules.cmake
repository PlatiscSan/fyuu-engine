function(collect_cxx_modules
	base_dir
	group_name
	out_interface_var
	out_internal_var
	out_implementation_var
)
	file(GLOB_RECURSE interface_modules CONFIGURE_DEPENDS
		"${base_dir}/*.ixx"
		"${base_dir}/*.cppm"
		"${base_dir}/*.mpp"
		"${base_dir}/*.cxxm"
		"${base_dir}/*.mxx"
	)
	file(GLOB_RECURSE internal_modules CONFIGURE_DEPENDS
		"${base_dir}/internal/*.ixx"
		"${base_dir}/internal/*.cppm"
		"${base_dir}/internal/*.mpp"
		"${base_dir}/internal/*.cxxm"
		"${base_dir}/internal/*.mxx"
		"${base_dir}/internal/*.cpp"
	)
	file(GLOB implementation_units CONFIGURE_DEPENDS
		"${base_dir}/*.cpp"
		"${base_dir}/impl/*.cpp"
	)
	list(REMOVE_ITEM interface_modules ${internal_modules})

	foreach(module_file IN LISTS interface_modules internal_modules implementation_units)
		file(RELATIVE_PATH relative_path "${base_dir}" "${module_file}")
		get_filename_component(directory_path "${relative_path}" DIRECTORY)
		if(group_name AND directory_path)
			set(source_group_path "${group_name}\\${directory_path}")
		elseif(group_name)
			set(source_group_path "${group_name}")
		else()
			set(source_group_path "${directory_path}")
		endif()
		if(source_group_path)
			source_group("Module\\${source_group_path}" FILES "${module_file}")
		else()
			source_group("Module" FILES "${module_file}")
		endif()
	endforeach()

	set(${out_interface_var} ${interface_modules} PARENT_SCOPE)
	set(${out_internal_var} ${internal_modules} PARENT_SCOPE)
	set(${out_implementation_var} ${implementation_units} PARENT_SCOPE)
endfunction()
