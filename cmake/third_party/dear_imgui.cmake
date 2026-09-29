set(TERRAINGEN_DEP_VERSION_dear_imgui
    v1.92.9b
    CACHE STRING "Tag of Dear ImGui to fetch.")
mark_as_advanced(TERRAINGEN_DEP_VERSION_dear_imgui)

include(FetchContent)

FetchContent_Declare(
    dear_imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG ${TERRAINGEN_DEP_VERSION_dear_imgui}
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(dear_imgui)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/dear_imgui"
                 "${CMAKE_BINARY_DIR}/_deps/dear_imgui-cmake")
