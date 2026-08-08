function(collect_headers base_dir group_name)

    file(GLOB_RECURSE headers
        "${base_dir}/*.h"
        "${base_dir}/*.hpp"
        "${base_dir}/*.hxx"
        "${base_dir}/*.hh"
        "${base_dir}/*.h++"
        "${base_dir}/*.H"
        "${base_dir}/*.ixx"
        "${base_dir}/*.inl"
        "${base_dir}/*.inc"
    )
	list(FILTER headers EXCLUDE REGEX "[/\\\\]deprecated[/\\\\]")
    
    file(GLOB_RECURSE tpl_headers
        "${base_dir}/*.tpp"
        "${base_dir}/*.txx"
        "${base_dir}/*.ipp"
    )
	list(FILTER tpl_headers EXCLUDE REGEX "[/\\\\]deprecated[/\\\\]")
    
    file(GLOB_RECURSE module_headers
        "${base_dir}/*.mpp"
        "${base_dir}/*.cxxm"
        "${base_dir}/*.mxx"
    )
	list(FILTER module_headers EXCLUDE REGEX "[/\\\\]deprecated[/\\\\]")
    
    set(all_headers ${headers} ${tpl_headers} ${module_headers})
    
    if(all_headers)
        source_group("Header\\${group_name}" FILES ${all_headers})
        
        if(tpl_headers)
            source_group("Header\\${group_name}\\templates" FILES ${tpl_headers})
        endif()
        
        if(module_headers)
            source_group("Header\\${group_name}\\modules" FILES ${module_headers})
        endif()
    endif()
    
    if(ARGV2)
        set(${ARGV2} ${all_headers} PARENT_SCOPE)
    endif()
    
    if(ARGV3)
        set(${ARGV3} ${tpl_headers} PARENT_SCOPE)
    endif()
    
    if(ARGV4)
        set(${ARGV4} ${module_headers} PARENT_SCOPE)
    endif()
    
    if(ARGV5)
        set(${ARGV5} ${headers} PARENT_SCOPE)
    endif()
endfunction()
