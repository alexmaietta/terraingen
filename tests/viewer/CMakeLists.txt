terraingen_add_test_suite(
    viewer
    SOURCES
    options_test.cpp
    settings_test.cpp
    worker_test.cpp
    LIBRARIES
    terraingen_viewer_app)
target_compile_definitions(terraingen_tests_viewer
                           PRIVATE TERRAINGEN_TEST_FIXTURES="${PROJECT_SOURCE_DIR}/tests/fixtures")

add_test(
    NAME viewer.smoke
    COMMAND
        ${CMAKE_COMMAND} -DVIEWER=$<TARGET_FILE:terraingen-viewer>
        -DTEXTURE=${PROJECT_SOURCE_DIR}/tests/fixtures/images/grey16.png -P
        "${CMAKE_CURRENT_SOURCE_DIR}/smoke_viewer.cmake")
set_tests_properties(viewer.smoke PROPERTIES LABELS gpu SKIP_REGULAR_EXPRESSION
                                             "No display; skipping")
set_property(GLOBAL APPEND PROPERTY TERRAINGEN_TEST_TARGETS terraingen-viewer)
