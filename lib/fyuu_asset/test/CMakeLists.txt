project(FyuuAssetImportTests LANGUAGES CXX)

add_executable(${PROJECT_NAME} import.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE FyuuAsset Boost::uuid)
disable_rtti(${PROJECT_NAME})

add_test(NAME FyuuAsset.Import COMMAND ${PROJECT_NAME})
set_tests_properties(FyuuAsset.Import PROPERTIES TIMEOUT 30)
