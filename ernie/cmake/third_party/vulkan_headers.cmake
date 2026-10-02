set(ERNIE_DEP_VERSION_vulkan_headers
    vulkan-sdk-1.4.363.0
    CACHE STRING "Tag of Vulkan-Headers to fetch.")
mark_as_advanced(ERNIE_DEP_VERSION_vulkan_headers)

include(FetchContent)

FetchContent_Declare(
    vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG ${ERNIE_DEP_VERSION_vulkan_headers}
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-build)
FetchContent_MakeAvailable(vulkan_headers)

add_library(ernie_vulkan_headers INTERFACE)
target_include_directories(ernie_vulkan_headers SYSTEM INTERFACE "${vulkan_headers_SOURCE_DIR}/include")
