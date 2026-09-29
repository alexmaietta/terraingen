set(TERRAINGEN_DEP_VERSION_googletest
    v1.18.0
    CACHE STRING "Tag of GoogleTest to fetch.")
mark_as_advanced(TERRAINGEN_DEP_VERSION_googletest)

include(FetchContent)

set(BUILD_GMOCK OFF)
set(INSTALL_GTEST OFF)
set(gtest_force_shared_crt ON)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG ${TERRAINGEN_DEP_VERSION_googletest}
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(googletest)
