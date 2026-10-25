# SPDX-License-Identifier: BSD-3-Clause

terraingen_add_test_suite(
    noise
    SOURCES
    fractal_rows_test.cpp
    fractal_test.cpp
    fractals_test.cpp
    hash_test.cpp
    noise3_test.cpp
    perlin_test.cpp
    simplex_test.cpp
    surface_noise_test.cpp
    structure_function_test.cpp
    validate_test.cpp
    value_test.cpp
    worley_test.cpp
    LIBRARIES
    terraingen::noise)
