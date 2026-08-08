project(FyuuAssetTests LANGUAGES CXX)

find_package(Boost REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(${PROJECT_NAME} main.cpp)

target_include_directories(${PROJECT_NAME} PRIVATE ${Boost_INCLUDE_DIRS})

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
		FyuuAsset
		nlohmann_json::nlohmann_json
)

disable_rtti(${PROJECT_NAME})

add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})
