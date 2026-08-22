function(collect_cxx_modules base_dir group_name out_public_var out_internal_var out_implementation_var)
	file(GLOB_RECURSE public_modules CONFIGURE_DEPENDS
		"${base_dir}/*.ixx"
		"${base_dir}/*.cppm"
		"${base_dir}/*.mpp"
		"${base_dir}/*.cxxm"
		"${base_dir}/*.mxx"
	)
	# Internal module partitions use .cpp by project convention. Keeping this
	# collection under module/internal prevents ordinary translation units from
	# being mistaken for CXX_MODULES file-set members.
	file(GLOB_RECURSE internal_modules CONFIGURE_DEPENDS
		"${base_dir}/internal/*.cpp"
	)
	set(implementation_units)
	foreach(module_file IN LISTS internal_modules)
		file(STRINGS "${module_file}" primary_module_declaration
			REGEX "^[ \t]*module[ \t]+[^:;]+[ \t]*;"
			LIMIT_COUNT 1
		)
		if(primary_module_declaration)
			list(APPEND implementation_units "${module_file}")
		endif()
	endforeach()
	list(REMOVE_ITEM internal_modules ${implementation_units})
	foreach(module_file IN LISTS public_modules internal_modules implementation_units)
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
	set(${out_public_var} ${public_modules} PARENT_SCOPE)
	set(${out_internal_var} ${internal_modules} PARENT_SCOPE)
	set(${out_implementation_var} ${implementation_units} PARENT_SCOPE)
endfunction()
