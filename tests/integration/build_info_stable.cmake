# SPDX-License-Identifier: BSD-3-Clause

# The build info keeps its build time, and nothing is relinked, when a build has nothing to do;
# rebuilding one of a module's objects regenerates it with a new time.
set(module_dir "${BUILD_DIR}/src")
set(build_info "${module_dir}/build_info.cpp")

function(build)
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build "${BUILD_DIR}" --target terraingen_build_info
        RESULT_VARIABLE result
        OUTPUT_QUIET)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Building terraingen_build_info failed with ${result}")
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
file(GLOB_RECURSE library "${BUILD_DIR}/lib/libterraingen_build_info.*"
     "${BUILD_DIR}/lib/terraingen_build_info.lib")
stamp("${build_info}" info_before)
file(TIMESTAMP "${library}" library_before "%Y-%m-%dT%H:%M:%S")
stamp("${module_dir}/git_state.txt" state_before)
execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 1.1)
build()
stamp("${build_info}" info_after)
file(TIMESTAMP "${library}" library_after "%Y-%m-%dT%H:%M:%S")
stamp("${module_dir}/git_state.txt" state_after)
if(NOT info_before STREQUAL info_after)
    message(FATAL_ERROR "A build without changes regenerated the build info:\n${info_after}")
endif()
if(NOT library_before STREQUAL library_after)
    message(FATAL_ERROR "A build without changes relinked ${library}")
endif()
if(NOT state_before STREQUAL state_after)
    message(FATAL_ERROR "A build without changes rewrote the git state")
endif()

file(GLOB objects "${module_dir}/raster/CMakeFiles/terraingen_raster.dir/*.o"
     "${module_dir}/raster/CMakeFiles/terraingen_raster.dir/*.obj")
list(GET objects 0 object)
file(TOUCH "${object}")
build()
stamp("${build_info}" info_rebuilt)
if(info_rebuilt STREQUAL info_after)
    message(FATAL_ERROR "Rebuilding a module's object did not regenerate the build info")
endif()

set(ENV{SOURCE_DATE_EPOCH} 0)
set(pinned "${WORK_DIR}/build_info.cpp")
execute_process(
    COMMAND
        ${CMAKE_COMMAND} -DMODE=source -DSTATE=${module_dir}/git_state.txt
        -DTEMPLATE=${SOURCE_DIR}/src/util/build_info.cpp.in -DOUTPUT=${pinned} -P
        ${SOURCE_DIR}/cmake/BuildInfo.cmake
    RESULT_VARIABLE result)
file(READ "${pinned}" content)
if(NOT result EQUAL 0 OR NOT content MATCHES "\"1970-01-01T00:00:00Z\"")
    message(FATAL_ERROR "SOURCE_DATE_EPOCH was not used for the build time:\n${content}")
endif()
