add_subdirectory(core)
add_subdirectory(kernels)
add_subdirectory(distributions)
add_subdirectory(features)
add_subdirectory(spatial)
add_subdirectory(surfaces)
if(TARGET terraingen_render)
    add_subdirectory(render)
endif()
if(TARGET _terraingen)
    add_subdirectory(python)
endif()
if(TARGET terraingen_viewer_app)
    add_subdirectory(apps/viewer)
endif()

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
    set_tests_properties(package.deb PROPERTIES LABELS integration)
endif()

if(TERRAINGEN_COVERAGE)
    find_program(GCOVR_EXECUTABLE gcovr HINTS "${PROJECT_SOURCE_DIR}/.venv/bin")
    if(NOT GCOVR_EXECUTABLE)
        message(FATAL_ERROR "TERRAINGEN_COVERAGE requires gcovr (pip install gcovr)")
    endif()

    string(REGEX MATCH "^[0-9]+" compiler_major "${CMAKE_CXX_COMPILER_VERSION}")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        find_program(GCOV_EXECUTABLE NAMES gcov-${compiler_major} gcov REQUIRED)
        set(gcov_command "${GCOV_EXECUTABLE}")
    else()
        find_program(LLVM_COV_EXECUTABLE NAMES llvm-cov-${compiler_major} llvm-cov REQUIRED)
        set(gcov_command "${LLVM_COV_EXECUTABLE} gcov")
    endif()

    set(report_dir "${PROJECT_BINARY_DIR}/coverage")
    add_custom_target(
        coverage
        COMMAND ${CMAKE_COMMAND} -E make_directory "${report_dir}"
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${PROJECT_BINARY_DIR}" --output-on-failure -LE
                validation
        COMMAND
            "${GCOVR_EXECUTABLE}" --root "${PROJECT_SOURCE_DIR}" --gcov-executable "${gcov_command}"
            --filter "${PROJECT_SOURCE_DIR}/include/terraingen/" --filter
            "${PROJECT_SOURCE_DIR}/src/" --filter "${PROJECT_SOURCE_DIR}/apps/"
            --exclude-throw-branches --exclude-noncode-lines --delete --txt --html-details
            "${report_dir}/index.html" --cobertura "${report_dir}/coverage.xml" --print-summary
            "${PROJECT_BINARY_DIR}"
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        COMMENT "Running tests and writing coverage reports to ${report_dir}"
        USES_TERMINAL VERBATIM)
    get_property(test_targets GLOBAL PROPERTY TERRAINGEN_TEST_TARGETS)
    add_dependencies(coverage ${test_targets})
endif()
