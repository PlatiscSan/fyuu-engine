cmake_minimum_required(VERSION 3.28)
project(VulkanHppModule)

add_library(${PROJECT_NAME} STATIC)

find_package(Vulkan REQUIRED)

if(MSVC)
    target_compile_options(${PROJECT_NAME} 
        PRIVATE 
            /GR-
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${PROJECT_NAME} 
        PRIVATE 
            -fno-rtti
    )
endif()

target_sources(${PROJECT_NAME}  
    PUBLIC
        FILE_SET CXX_MODULES
        TYPE CXX_MODULES
        BASE_DIRS ${Vulkan_INCLUDE_DIR}
        FILES ${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm
)
target_compile_features(${PROJECT_NAME} 
    PUBLIC 
        cxx_std_20
)
target_link_libraries(${PROJECT_NAME} 
    PUBLIC 
        Vulkan::Vulkan
)

target_compile_definitions(${PROJECT_NAME} 
    PUBLIC
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
)

if(WIN32)
target_compile_definitions(${PROJECT_NAME} 
    PRIVATE
        VK_USE_PLATFORM_WIN32_KHR
)
elseif(UNIX)
target_compile_definitions(${PROJECT_NAME} 
    PRIVATE
        VK_USE_PLATFORM_WAYLAND_KHR
        VK_USE_PLATFORM_XLIB_KHR
)
elseif(ANDROID)
target_compile_definitions(${PROJECT_NAME} 
    PRIVATE
        VkAndroidSurfaceCreateInfoKHR
)
endif(WIN32)
