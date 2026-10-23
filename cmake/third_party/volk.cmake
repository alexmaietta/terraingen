# SPDX-License-Identifier: BSD-3-Clause

set(TERRAINGEN_DEP_VERSION_volk
    vulkan-sdk-1.4.363.0
    CACHE STRING "Tag of volk to fetch.")
mark_as_advanced(TERRAINGEN_DEP_VERSION_volk)

include(FetchContent)

# volk's own build is not used; it is one C file compiled here against the fetched headers. It opens
# the Vulkan loader (or MoltenVK) at run time, so nothing links against libvulkan.
FetchContent_Declare(
    terraingen_volk_src
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG ${TERRAINGEN_DEP_VERSION_volk}
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-build)
FetchContent_MakeAvailable(terraingen_volk_src)

add_library(terraingen_volk STATIC "${terraingen_volk_src_SOURCE_DIR}/volk.c")
target_include_directories(terraingen_volk SYSTEM
                           PUBLIC "$<BUILD_INTERFACE:${terraingen_volk_src_SOURCE_DIR}>")
target_compile_definitions(terraingen_volk PUBLIC VK_NO_PROTOTYPES)
target_link_libraries(terraingen_volk PUBLIC terraingen_vulkan_headers ${CMAKE_DL_LIBS})
set_target_properties(terraingen_volk PROPERTIES POSITION_INDEPENDENT_CODE ON)
