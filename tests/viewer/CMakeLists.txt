terraingen_add_test_suite(
    viewer
    SOURCES
    terrain_settings_test.cpp
    generation_worker_test.cpp
    view_settings_test.cpp
    LIBRARIES
    terraingen_viewer_app)

add_test(NAME viewer.smoke COMMAND ${CMAKE_COMMAND} -DVIEWER=$<TARGET_FILE:terraingen-viewer> -P
                                   "${CMAKE_CURRENT_SOURCE_DIR}/smoke_viewer.cmake")
set_tests_properties(viewer.smoke PROPERTIES LABELS gpu SKIP_REGULAR_EXPRESSION
                                             "No display; skipping")
set_property(GLOBAL APPEND PROPERTY TERRAINGEN_TEST_TARGETS terraingen-viewer)
