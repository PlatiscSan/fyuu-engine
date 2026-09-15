find_package(Doxygen REQUIRED)

set(DOXYGEN_PROJECT_NAME "FyuuRHI")
set(DOXYGEN_PROJECT_BRIEF "FyuuEngine rendering hardware interface")
set(DOXYGEN_EXTRACT_ALL NO)
set(DOXYGEN_EXTRACT_PRIVATE NO)
set(DOXYGEN_EXTRACT_STATIC NO)
set(DOXYGEN_JAVADOC_AUTOBRIEF YES)
set(DOXYGEN_MARKDOWN_SUPPORT YES)
set(DOXYGEN_FULL_PATH_NAMES NO)
set(DOXYGEN_STRIP_FROM_PATH "${PROJECT_SOURCE_DIR}")
set(DOXYGEN_WARN_IF_UNDOCUMENTED NO)
set(DOXYGEN_GENERATE_HTML YES)
set(DOXYGEN_GENERATE_XML YES)
set(DOXYGEN_GENERATE_LATEX NO)
set(DOXYGEN_RECURSIVE NO)
set(DOXYGEN_FILE_PATTERNS "*.cppm" "*.dox")
set(DOXYGEN_EXTENSION_MAPPING "cppm=C++")
set(DOXYGEN_EXCLUDE_SYMBOLS "fyuu_rhi::details::*" "fyuu_rhi::execution::details::*" "fyuu_rhi::*::data::*")

set(FYUU_RHI_DOCUMENTATION_SOURCES
	${PROJECT_SOURCE_DIR}/module/core.cppm
	${PROJECT_SOURCE_DIR}/module/instance.cppm
	${PROJECT_SOURCE_DIR}/module/physical_device.cppm
	${PROJECT_SOURCE_DIR}/module/logical_device.cppm
	${PROJECT_SOURCE_DIR}/module/resource.cppm
	${PROJECT_SOURCE_DIR}/module/view.cppm
	${PROJECT_SOURCE_DIR}/module/sampler.cppm
	${PROJECT_SOURCE_DIR}/module/pipeline.cppm
	${PROJECT_SOURCE_DIR}/module/execution.cppm
)

set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/en")
doxygen_add_docs(
	FyuuRHIDocumentation
	${CMAKE_CURRENT_SOURCE_DIR}/mainpage.dox
	${FYUU_RHI_DOCUMENTATION_SOURCES}
	COMMENT "Generating FyuuRHI English API documentation"
)

set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/zh-CN")
doxygen_add_docs(
	FyuuRHIDocumentationZhCN
	${CMAKE_CURRENT_SOURCE_DIR}/mainpage.zh-CN.dox
	${FYUU_RHI_DOCUMENTATION_SOURCES}
	COMMENT "Generating FyuuRHI Simplified Chinese API documentation"
)
