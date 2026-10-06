# Get git sha
execute_process(
    COMMAND git rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${SOURCE_DIR}
    OUTPUT_VARIABLE GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET
    RESULT_VARIABLE GIT_SHA_RESULT)
if(NOT GIT_SHA_RESULT EQUAL 0 OR GIT_SHA STREQUAL "")
    # Not a git checkout (e.g. building from a GitHub Releases source
    # tarball) -- fall back rather than fail the build.
    set(GIT_SHA "unknown")
endif()

# Check if git tree is dirty
set(GIT_DIRTY 0)
if(NOT GIT_SHA STREQUAL "unknown")
    execute_process(
        COMMAND git diff --quiet HEAD --
        WORKING_DIRECTORY ${SOURCE_DIR}
        RESULT_VARIABLE GIT_DIRTY_RESULT
        ERROR_QUIET)
    if(NOT GIT_DIRTY_RESULT EQUAL 0)
        set(GIT_DIRTY 1)
    endif()
endif()

# The build day rather than the time, so the file only changes, and the library and everything
# linking it only rebuild, when the commit, the dirty state or the day changes. TIMESTAMP honours
# SOURCE_DATE_EPOCH for reproducible builds.
string(TIMESTAMP BUILD_DATE "%Y-%m-%d" UTC)

# configure_file leaves an unchanged file untouched, so its timestamp does not trigger a rebuild.
configure_file(${TEMPLATE} ${OUTPUT} @ONLY)
