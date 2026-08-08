function(collect_sources base_dir group_name)

    file(GLOB_RECURSE sources CONFIGURE_DEPENDS
        "${base_dir}/*.cpp"
        "${base_dir}/*.cxx"
        "${base_dir}/*.cc"
        "${base_dir}/*.c"
        "${base_dir}/*.c++"
        "${base_dir}/*.C"
    )
	list(FILTER sources EXCLUDE REGEX "[/\\\\]deprecated[/\\\\]")
    
    set(all_sources "")
    
    foreach(source_file ${sources})
        file(RELATIVE_PATH rel_path ${base_dir} ${source_file})
        get_filename_component(dir_path ${rel_path} DIRECTORY)
        
        if(dir_path STREQUAL "")
            set(dir_group "${group_name}")
        else()
            set(dir_group "${group_name}\\${dir_path}")
        endif()
        
        source_group("Source\\${dir_group}" FILES ${source_file})
        list(APPEND all_sources ${source_file})
    endforeach()
    
    if(ARGV2)
        set(${ARGV2} ${all_sources} PARENT_SCOPE)
    endif()

endfunction()
