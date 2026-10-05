set(ERNIE_DEP_VERSION_implot
    v1.0
    CACHE STRING "Tag of ImPlot to fetch.")
mark_as_advanced(ERNIE_DEP_VERSION_implot)

include(FetchContent)

# ImPlot has no CMake build of its own; its two sources are compiled here against Dear ImGui.
FetchContent_Declare(
    implot
    GIT_REPOSITORY https://github.com/epezent/implot.git
    GIT_TAG ${ERNIE_DEP_VERSION_implot}
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(implot)

add_library(ernie_implot STATIC "${implot_SOURCE_DIR}/implot.cpp"
                                "${implot_SOURCE_DIR}/implot_items.cpp")
target_include_directories(ernie_implot SYSTEM PUBLIC "${implot_SOURCE_DIR}")
target_link_libraries(ernie_implot PUBLIC dear_imgui)
