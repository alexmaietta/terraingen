terraingen_add_test_suite(
    validation
    SOURCES
    size_frequency_check.cpp
    LIBRARIES
    terraingen::core
    LABELS
    validation)
target_compile_definitions(
    terraingen_tests_validation
    PRIVATE TERRAINGEN_VALIDATION_REFERENCES="${PROJECT_SOURCE_DIR}/validation/references")
