set(ERNIE_DEP_VERSION_imguizmo
    1.10
    CACHE STRING "Tag of ImGuizmo to fetch.")
mark_as_advanced(ERNIE_DEP_VERSION_imguizmo)

include(FetchContent)

# Only the transform gizmo (ImGuizmo.cpp) is compiled; the graph and sequencer editors are unused.
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG ${ERNIE_DEP_VERSION_imguizmo}
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR no-build)
FetchContent_MakeAvailable(imguizmo)

add_library(ernie_imguizmo STATIC "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
target_include_directories(ernie_imguizmo SYSTEM PUBLIC "${imguizmo_SOURCE_DIR}/src")
target_link_libraries(ernie_imguizmo PUBLIC dear_imgui)
