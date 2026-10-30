# SPDX-License-Identifier: BSD-3-Clause

terraingen_add_test_suite(
    pipeline
    SOURCES
    features_test.cpp
    ground_reads_test.cpp
    materials_test.cpp
    mesh_build_test.cpp
    report_test.cpp
    terrain_test.cpp
    terrain_windows_test.cpp
    LIBRARIES
    terraingen::pipeline
    terraingen::io
    nlohmann_json)
