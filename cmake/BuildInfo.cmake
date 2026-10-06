# Build info for terraingen::core, in two steps run with -P:
#
# MODE=git_state (every build): writes the checkout's short commit SHA and dirty flag to OUTPUT,
# leaving the file untouched when they have not changed.
#
# MODE=source (only when the library's objects or OUTPUT of the git_state step change): writes the
# build info source from TEMPLATE to OUTPUT with the git state in STATE and the current time, so the
# time is when the library was last rebuilt and builds without changes relink nothing. TIMESTAMP
# honours SOURCE_DATE_EPOCH for reproducible builds.
if(MODE STREQUAL "git_state")
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
elseif(MODE STREQUAL "source")
    file(READ "${STATE}" state)
    string(STRIP "${state}" state)
    list(GET state 0 GIT_SHA)
    list(GET state 1 GIT_DIRTY)
    string(TIMESTAMP BUILD_DATE "%Y-%m-%dT%H:%M:%SZ" UTC)
    configure_file(${TEMPLATE} ${OUTPUT} @ONLY)
else()
    message(FATAL_ERROR "BuildInfo.cmake needs MODE=git_state or MODE=source, not '${MODE}'")
endif()
