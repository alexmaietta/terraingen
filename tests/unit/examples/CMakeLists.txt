terraingen_add_test_suite(examples SOURCES example_harness_test.cpp LIBRARIES
                          terraingen_examples_common)
if(TERRAINGEN_GUI)
    terraingen_add_test_suite(
        examples_browser
        SOURCES
        example_browser_test.cpp
        "${PROJECT_SOURCE_DIR}/examples/cpp/common/browser.cpp"
        LIBRARIES
        terraingen_examples_common
        terraingen_gui
        LABELS
        gpu)
endif()
