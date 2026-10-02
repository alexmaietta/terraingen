set(ERNIE_DEP_VERSION_vma
    v3.4.0
    CACHE STRING "Tag of Vulkan Memory Allocator to fetch.")
mark_as_advanced(ERNIE_DEP_VERSION_vma)

include(FetchContent)

FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG ${ERNIE_DEP_VERSION_vma}
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-build)
FetchContent_MakeAvailable(vma)

# The allocator is header-only; vma/vma.cpp compiles its implementation once, fetching Vulkan
# functions through volk.
add_library(ernie_vma STATIC "${CMAKE_CURRENT_LIST_DIR}/vma/vma.cpp")
target_include_directories(ernie_vma SYSTEM PUBLIC "${vma_SOURCE_DIR}/include")
target_compile_definitions(ernie_vma PUBLIC VMA_STATIC_VULKAN_FUNCTIONS=0
                                            VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
target_link_libraries(ernie_vma PUBLIC ernie_volk)
set_target_properties(ernie_vma PROPERTIES POSITION_INDEPENDENT_CODE ON)
