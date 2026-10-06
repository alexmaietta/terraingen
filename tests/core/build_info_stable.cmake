# The build info keeps its build time, and the library is not relinked, when a build has nothing to
# do; rebuilding one of the library's objects regenerates it with a new time.
set(core_dir "${BUILD_DIR}/terraingen/src")
set(build_info "${core_dir}/build_info.cpp")

function(build)
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${BUILD_DIR}" --target terraingen_core
        RESULT_VARIABLE result
        OUTPUT_QUIET)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Building terraingen_core failed with ${result}")
    endif()
endfunction()

function(stamp path variable)
    file(TIMESTAMP "${path}" time "%Y-%m-%dT%H:%M:%S")
    file(READ "${path}" content)
    set(${variable}
        "${time}\n${content}"
        PARENT_SCOPE)
endfunction()

build()
file(GLOB_RECURSE library "${BUILD_DIR}/lib/libterraingen_core.*")
stamp("${build_info}" info_before)
file(TIMESTAMP "${library}" library_before "%Y-%m-%dT%H:%M:%S")
stamp("${core_dir}/git_state.txt" state_before)
execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 1.1)
build()
stamp("${build_info}" info_after)
file(TIMESTAMP "${library}" library_after "%Y-%m-%dT%H:%M:%S")
stamp("${core_dir}/git_state.txt" state_after)
if(NOT info_before STREQUAL info_after)
    message(FATAL_ERROR "A build without changes regenerated the build info:\n${info_after}")
endif()
if(NOT library_before STREQUAL library_after)
    message(FATAL_ERROR "A build without changes relinked ${library}")
endif()
if(NOT state_before STREQUAL state_after)
    message(FATAL_ERROR "A build without changes rewrote the git state")
endif()

file(GLOB_RECURSE objects "${core_dir}/CMakeFiles/terraingen_core_objects.dir/*.o")
list(GET objects 0 object)
file(TOUCH "${object}")
build()
stamp("${build_info}" info_rebuilt)
if(info_rebuilt STREQUAL info_after)
    message(FATAL_ERROR "Rebuilding an object did not regenerate the build info")
endif()

set(ENV{SOURCE_DATE_EPOCH} 0)
set(pinned "${WORK_DIR}/build_info.cpp")
execute_process(
    COMMAND
        ${CMAKE_COMMAND} -DSTATE=${core_dir}/git_state.txt
        -DTEMPLATE=${SOURCE_DIR}/terraingen/src/core/build_info.cpp.in -DOUTPUT=${pinned} -P
        ${SOURCE_DIR}/cmake/GenerateBuildInfo.cmake
    RESULT_VARIABLE result)
file(READ "${pinned}" content)
if(NOT result EQUAL 0 OR NOT content MATCHES "\"1970-01-01T00:00:00Z\"")
    message(FATAL_ERROR "SOURCE_DATE_EPOCH was not used for the build time:\n${content}")
endif()
