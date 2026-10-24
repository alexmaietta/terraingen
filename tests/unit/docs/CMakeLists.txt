# SPDX-License-Identifier: BSD-3-Clause

# The C++ snippets of the documentation (docs/snippets/<category>/*.cpp), each a test whose marked
# lines a page shows.
file(GLOB snippets CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/docs/snippets/*/*.cpp")
terraingen_add_test_suite(docs_snippets SOURCES ${snippets} LIBRARIES terraingen::terraingen)
