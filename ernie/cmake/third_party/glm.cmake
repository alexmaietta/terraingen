set(ERNIE_DEP_VERSION_glm
    1.0.3
    CACHE STRING "Tag of glm to fetch.")
mark_as_advanced(ERNIE_DEP_VERSION_glm)

include(FetchContent)

set(GLM_BUILD_LIBRARY OFF)
set(GLM_BUILD_TESTS OFF)
set(GLM_BUILD_INSTALL OFF)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG ${ERNIE_DEP_VERSION_glm}
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glm)

add_library(ernie_glm INTERFACE)
target_include_directories(ernie_glm SYSTEM INTERFACE "${glm_SOURCE_DIR}")
target_compile_definitions(ernie_glm INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE)
