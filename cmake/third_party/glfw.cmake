set(TERRAINGEN_DEP_VERSION_glfw
    3.4
    CACHE STRING "Tag of GLFW to fetch when no system GLFW >= 3.4 is found.")
mark_as_advanced(TERRAINGEN_DEP_VERSION_glfw)

option(TERRAINGEN_USE_SYSTEM_GLFW
       "Use an installed GLFW >= 3.4 if found; turn off for packages so GLFW is linked statically"
       ON)

if(TERRAINGEN_USE_SYSTEM_GLFW)
    find_package(glfw3 3.4 QUIET)
endif()
if(NOT glfw3_FOUND)
    include(FetchContent)

    set(GLFW_BUILD_DOCS OFF)
    set(GLFW_BUILD_TESTS OFF)
    set(GLFW_BUILD_EXAMPLES OFF)
    set(GLFW_INSTALL OFF)

    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG ${TERRAINGEN_DEP_VERSION_glfw}
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glfw)
endif()
