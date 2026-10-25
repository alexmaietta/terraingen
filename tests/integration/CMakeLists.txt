# SPDX-License-Identifier: BSD-3-Clause

# Tests of the build, install and packages, run against the build tree.

# Builds the library, so it runs alone to keep its relink away from tests running binaries.
add_test(
    NAME core.build_info_stable
    COMMAND
        ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DBUILD_DIR=${PROJECT_BINARY_DIR}
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR} -P
        "${CMAKE_CURRENT_SOURCE_DIR}/build_info_stable.cmake")
set_tests_properties(core.build_info_stable PROPERTIES LABELS integration RUN_SERIAL TRUE)

if(TERRAINGEN_INSTALL AND NOT TERRAINGEN_COVERAGE)
    set(package_prefix "${CMAKE_CURRENT_BINARY_DIR}/package/prefix")
    add_test(NAME package.install COMMAND ${CMAKE_COMMAND} --install ${PROJECT_BINARY_DIR} --config
                                          $<CONFIG> --prefix ${package_prefix})
    add_test(
        NAME package.consumer
        COMMAND
            ${CMAKE_CTEST_COMMAND} --build-and-test "${CMAKE_CURRENT_SOURCE_DIR}/package"
            "${CMAKE_CURRENT_BINARY_DIR}/package/build" --build-generator ${CMAKE_GENERATOR}
            --build-config $<CONFIG> --build-options "-DCMAKE_PREFIX_PATH=${package_prefix}"
            --test-command package_test)
    set_tests_properties(package.install PROPERTIES FIXTURES_SETUP terraingen_package LABELS
                                                    integration)
    set_tests_properties(package.consumer PROPERTIES FIXTURES_REQUIRED terraingen_package LABELS
                                                     integration)
endif()

find_program(DPKG_DEB_EXECUTABLE dpkg-deb)
if(TERRAINGEN_INSTALL
   AND NOT TERRAINGEN_COVERAGE
   AND DPKG_DEB_EXECUTABLE)
    add_test(
        NAME package.deb
        COMMAND
            ${CMAKE_COMMAND} -DCPACK=${CMAKE_CPACK_COMMAND} -DCTEST=${CMAKE_CTEST_COMMAND}
            -DBUILD_DIR=${PROJECT_BINARY_DIR} -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/package/deb
            -DCONSUMER_DIR=${CMAKE_CURRENT_SOURCE_DIR}/package -DGENERATOR=${CMAKE_GENERATOR} -P
            "${CMAKE_CURRENT_SOURCE_DIR}/package/test_debian_packages.cmake")
    # CPack builds the project before packaging; running alone keeps that build from relinking
    # binaries other tests are running.
    set_tests_properties(package.deb PROPERTIES LABELS integration RUN_SERIAL TRUE)
endif()
