set(ERNIE_DEP_VERSION_moltenvk
    v1.4.2
    CACHE STRING "Release of MoltenVK to fetch. Changing it requires updating the hash below.")
mark_as_advanced(ERNIE_DEP_VERSION_moltenvk)

# MoltenVK implements Vulkan on Metal. On macOS Ernie loads its dylib directly through volk, so no
# Vulkan loader is needed; the viewer and wheels ship it beside the binaries.
include(FetchContent)

FetchContent_Declare(
    moltenvk
    URL "https://github.com/KhronosGroup/MoltenVK/releases/download/${ERNIE_DEP_VERSION_moltenvk}/MoltenVK-macos.tar"
    URL_HASH SHA256=f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e
    SOURCE_SUBDIR no-build)
FetchContent_MakeAvailable(moltenvk)

set(ERNIE_MOLTENVK_LIBRARY
    "${moltenvk_SOURCE_DIR}/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib"
    CACHE INTERNAL "MoltenVK dylib that Ernie loads on macOS")
if(NOT EXISTS "${ERNIE_MOLTENVK_LIBRARY}")
    file(GLOB_RECURSE moltenvk_candidates "${moltenvk_SOURCE_DIR}/*/libMoltenVK.dylib")
    message(FATAL_ERROR "libMoltenVK.dylib not at the expected path; found: ${moltenvk_candidates}")
endif()
