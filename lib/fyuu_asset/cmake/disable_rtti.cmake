function(disable_rtti target_name)
    # Disable RTTI for the specified target
    set_target_properties(${target_name} PROPERTIES
        CXX_RTTI OFF
    )
    
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /GR-)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target_name} PRIVATE -fno-rtti)
    endif()
    
endfunction()