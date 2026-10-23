# SPDX-License-Identifier: BSD-3-Clause

terraingen_add_test_suite(
    random
    SOURCES
    philox_test.cpp
    samplers_test.cpp
    stream_test.cpp
    value_distribution_test.cpp
    LIBRARIES
    terraingen::random)
