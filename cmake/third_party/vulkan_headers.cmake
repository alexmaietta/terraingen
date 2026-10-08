set(TERRAINGEN_DEP_VERSION_vulkan_headers
    vulkan-sdk-1.4.363.0
    CACHE STRING "Tag of Vulkan-Headers to fetch.")
mark_as_advanced(TERRAINGEN_DEP_VERSION_vulkan_headers)

include(FetchContent)

FetchContent_Declare(
    terraingen_vulkan_headers_src
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG ${TERRAINGEN_DEP_VERSION_vulkan_headers}
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-build)
FetchContent_MakeAvailable(terraingen_vulkan_headers_src)

add_library(terraingen_vulkan_headers INTERFACE)
target_include_directories(terraingen_vulkan_headers SYSTEM
                           INTERFACE "$<BUILD_INTERFACE:${terraingen_vulkan_headers_src_SOURCE_DIR}/include>")
