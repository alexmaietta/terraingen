# Regenerating the build info without source changes must leave the file untouched, so builds
# without changes do not recompile it and relink everything that links the library.
set(output "${WORK_DIR}/build_info.cpp")
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

function(generate)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${SOURCE_DIR} -DTEMPLATE=${TEMPLATE}
                -DOUTPUT=${output} -P ${SOURCE_DIR}/cmake/GenerateBuildInfo.cmake
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "GenerateBuildInfo.cmake failed with ${result}")
    endif()
endfunction()

generate()
file(READ "${output}" first)
file(TIMESTAMP "${output}" first_time "%Y-%m-%dT%H:%M:%S")
execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 1.1)
generate()
file(READ "${output}" second)
file(TIMESTAMP "${output}" second_time "%Y-%m-%dT%H:%M:%S")
if(NOT first STREQUAL second)
    message(FATAL_ERROR "Build info changed between builds:\n${first}\n${second}")
endif()
if(NOT first_time STREQUAL second_time)
    message(
        FATAL_ERROR "Build info was rewritten although unchanged (${first_time}, ${second_time})")
endif()

set(ENV{SOURCE_DATE_EPOCH} 0)
generate()
file(READ "${output}" pinned)
if(NOT pinned MATCHES "\"1970-01-01\"")
    message(FATAL_ERROR "SOURCE_DATE_EPOCH was not used for the build date:\n${pinned}")
endif()
