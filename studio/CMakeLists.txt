cmake_minimum_required(VERSION 4.2.3)

project(FyuuStudio LANGUAGES NONE)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
	include_external_msproject(
		FyuuStudio
		"${CMAKE_CURRENT_SOURCE_DIR}/FyuuStudio.csproj"
		TYPE FAE04EC0-301F-11D3-BF4B-00C04F79EFBC
		PLATFORM AnyCPU
		FyuuEngine
	)
else()
	find_program(DOTNET_EXECUTABLE dotnet REQUIRED)
	add_custom_target(FyuuStudio
		COMMAND ${DOTNET_EXECUTABLE} build
			"${CMAKE_CURRENT_SOURCE_DIR}/FyuuStudio.csproj"
			--configuration Release
			--property:FyuuNativeOutputDir=$<TARGET_FILE_DIR:FyuuEngine>
			--property:OutputPath=${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/FyuuStudio/
			--property:BaseIntermediateOutputPath=${CMAKE_BINARY_DIR}/obj/FyuuStudio/
			--property:MSBuildProjectExtensionsPath=${CMAKE_BINARY_DIR}/obj/FyuuStudio/
		DEPENDS FyuuEngine
		USES_TERMINAL
	)
endif()
