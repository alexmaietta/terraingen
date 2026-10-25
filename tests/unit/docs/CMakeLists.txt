# SPDX-License-Identifier: BSD-3-Clause

file(GLOB snippets CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/docs/snippets/*/*.cpp")
terraingen_add_test_suite(docs_snippets SOURCES ${snippets} LIBRARIES terraingen::terraingen)
target_include_directories(terraingen_tests_docs_snippets
                           PRIVATE "${PROJECT_SOURCE_DIR}/docs/snippets")
