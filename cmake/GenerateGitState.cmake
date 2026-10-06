# Writes the checkout's short commit SHA and dirty flag to OUTPUT, leaving the file untouched when
# they have not changed so the build info is only regenerated when they do.
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

file(WRITE "${OUTPUT}.tmp" "${GIT_SHA};${GIT_DIRTY}\n")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
