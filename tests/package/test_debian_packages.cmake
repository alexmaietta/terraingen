file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/root")

execute_process(
    COMMAND "${CPACK}" -G DEB -B "${WORK_DIR}/debs"
    WORKING_DIRECTORY "${BUILD_DIR}"
    RESULT_VARIABLE result
    OUTPUT_QUIET)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "cpack failed with ${result}")
endif()

foreach(package terraingen-dev)
    file(GLOB deb "${WORK_DIR}/debs/${package}_*.deb")
    if(NOT deb)
        message(FATAL_ERROR "${package} package was not generated")
    endif()
    execute_process(COMMAND dpkg-deb -x "${deb}" "${WORK_DIR}/root" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Could not extract ${deb}")
    endif()
endforeach()

execute_process(
    COMMAND
        "${CTEST}" --build-and-test "${CONSUMER_DIR}" "${WORK_DIR}/consumer" --build-generator
        "${GENERATOR}" --build-options "-DCMAKE_PREFIX_PATH=${WORK_DIR}/root/usr" --test-command
        package_test
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Consumer build against terraingen-dev failed with ${result}")
endif()
