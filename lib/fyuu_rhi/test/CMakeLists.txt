project(FyuuRHIResourceSubmissionTests LANGUAGES CXX)

add_executable(${PROJECT_NAME}
	resource_submission.cpp
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
disable_rtti(${PROJECT_NAME})
target_include_directories(${PROJECT_NAME}
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/../src/graph
)

add_test(
	NAME FyuuRHI.ResourceSubmission
	COMMAND ${PROJECT_NAME}
)

add_executable(FyuuRHIResourceIdentityTests
	resource_identity.cpp
)

target_compile_features(FyuuRHIResourceIdentityTests PRIVATE cxx_std_23)
disable_rtti(FyuuRHIResourceIdentityTests)
target_link_libraries(FyuuRHIResourceIdentityTests PRIVATE FyuuRHI)

add_test(
	NAME FyuuRHI.ResourceIdentity
	COMMAND FyuuRHIResourceIdentityTests
)

add_executable(FyuuRHIResourceTransferTests
	resource_transfer.cpp
)

target_compile_features(FyuuRHIResourceTransferTests PRIVATE cxx_std_23)
disable_rtti(FyuuRHIResourceTransferTests)
target_link_libraries(FyuuRHIResourceTransferTests PRIVATE FyuuRHI)

add_test(
	NAME FyuuRHI.ResourceTransfer
	COMMAND FyuuRHIResourceTransferTests
)

add_executable(FyuuRHITextureTransferIntegrationTests
	texture_transfer_integration.cpp
)

target_compile_features(FyuuRHITextureTransferIntegrationTests PRIVATE cxx_std_23)
disable_rtti(FyuuRHITextureTransferIntegrationTests)
target_link_libraries(FyuuRHITextureTransferIntegrationTests PRIVATE FyuuRHI)

if(WIN32)
	add_custom_command(
		TARGET FyuuRHITextureTransferIntegrationTests
		POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_FILE:Microsoft::DirectXShaderCompiler>
			$<TARGET_FILE_DIR:FyuuRHITextureTransferIntegrationTests>
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_FILE:Microsoft::DXIL>
			$<TARGET_FILE_DIR:FyuuRHITextureTransferIntegrationTests>
		VERBATIM
	)
endif()

if(WIN32)
	add_test(
		NAME FyuuRHI.TextureTransfer.D3D12
		COMMAND FyuuRHITextureTransferIntegrationTests d3d12
	)
	add_test(
		NAME FyuuRHI.TextureTransfer.OpenGL
		COMMAND FyuuRHITextureTransferIntegrationTests opengl
	)
endif()

if(NOT APPLE)
	add_test(
		NAME FyuuRHI.TextureTransfer.Vulkan
		COMMAND FyuuRHITextureTransferIntegrationTests vulkan
	)
endif()

add_test(
	NAME FyuuRHI.TextureTransfer.WebGPU
	COMMAND FyuuRHITextureTransferIntegrationTests webgpu
)

set_tests_properties(
	FyuuRHI.TextureTransfer.WebGPU
	PROPERTIES LABELS "RHI;Integration;GPU"
)
if(WIN32)
	set_tests_properties(
		FyuuRHI.TextureTransfer.D3D12
		FyuuRHI.TextureTransfer.OpenGL
		PROPERTIES LABELS "RHI;Integration;GPU"
	)
endif()
if(NOT APPLE)
	set_tests_properties(
		FyuuRHI.TextureTransfer.Vulkan
		PROPERTIES LABELS "RHI;Integration;GPU"
	)
endif()
