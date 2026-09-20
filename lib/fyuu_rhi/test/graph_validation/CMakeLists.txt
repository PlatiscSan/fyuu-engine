project(GraphValidation LANGUAGES CXX)

add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE FyuuRHI)
disable_rtti(${PROJECT_NAME})

if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/dxcompiler.dll"
			"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/dxil.dll"
			"$<TARGET_FILE_DIR:${PROJECT_NAME}>/"
	)
endif()

# Registered once, without a backend argument. The command graph compiler in execution.cppm is
# backend-independent: connect() validates the graph and plans its batches and barriers before any
# device is involved, so this test needs no GPU, no window, and no RHI context. It is the only
# place the compiler's rejection rules are checked without a backend happening to make the same
# mistake.
add_test(NAME GraphValidation COMMAND GraphValidation)
set_tests_properties(GraphValidation PROPERTIES TIMEOUT 30)
