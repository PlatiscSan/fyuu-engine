function(collect_cxx_modules base_dir group_name out_all_var)
    file(GLOB_RECURSE all_module_files CONFIGURE_DEPENDS
        "${base_dir}/*.ixx"
        "${base_dir}/*.cppm"
        "${base_dir}/*.mpp"
        "${base_dir}/*.cxxm"
        "${base_dir}/*.mxx"
    )
	list(FILTER all_module_files EXCLUDE REGEX "[/\\\\]deprecated[/\\\\]")
	list(FILTER all_module_files EXCLUDE REGEX "[/\\\\]internal[/\\\\]")
    
    file(GLOB_RECURSE all_impl_files CONFIGURE_DEPENDS
        "${base_dir}/*.impl.cpp"
        "${base_dir}/*.impl.cxx"
        "${base_dir}/*.impl.cc"
    )
	list(FILTER all_impl_files EXCLUDE REGEX "[/\\\\]deprecated[/\\\\]")
    
    set(modules_by_dir "")
    set(impls_by_dir "")
    
    foreach(module_file ${all_module_files})
        file(RELATIVE_PATH rel_path ${base_dir} ${module_file})
        get_filename_component(dir_path ${rel_path} DIRECTORY)
        
        if(dir_path STREQUAL "")
            set(dir_group "${group_name}")
        else()
            set(dir_group "${group_name}\\${dir_path}")
        endif()
        
        source_group("Module\\${dir_group}" FILES ${module_file})
        list(APPEND modules_by_dir ${module_file})
    endforeach()
    
    foreach(impl_file ${all_impl_files})
        file(RELATIVE_PATH rel_path ${base_dir} ${impl_file})
        get_filename_component(dir_path ${rel_path} DIRECTORY)
        
        if(dir_path STREQUAL "")
            set(dir_group "${group_name}")
        else()
            set(dir_group "${group_name}\\${dir_path}")
        endif()
        
        source_group("Implementation\\${dir_group}" FILES ${impl_file})
        list(APPEND impls_by_dir ${impl_file})
    endforeach()
    
    set(all_files ${modules_by_dir} ${impls_by_dir})
    
    set(${out_all_var} ${all_files} PARENT_SCOPE)
endfunction()
