# SPDX-License-Identifier: BSD-3-Clause

include("${PROJECT_SOURCE_DIR}/cmake/third_party/benchmark.cmake")

# Timings are only meaningful from an optimized build: configure with -DCMAKE_BUILD_TYPE=Release.
add_executable(
    terraingen_benchmarks bench_boulders.cpp bench_craters.cpp bench_grids.cpp
                          bench_ground_reads.cpp bench_point_queries.cpp bench_sources.cpp)
target_include_directories(terraingen_benchmarks PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
                                                         "${PROJECT_SOURCE_DIR}/tests")
target_link_libraries(terraingen_benchmarks PRIVATE terraingen::terraingen benchmark::benchmark
                                                    terraingen_warnings)

if(TERRAINGEN_BUILD_TESTS)
    # One iteration of the smallest case of each benchmark, to catch benchmarks that no longer run.
    add_test(
        NAME benchmarks.smoke
        COMMAND
            terraingen_benchmarks --benchmark_min_time=1x --benchmark_format=json
            "--benchmark_filter=GenerateBoulderShape/3|GenerateCraters/256$|ApplyCraters/cells:1024/threads:0|EvaluateGrid/cells:512/threads:0|HeightAtWarm/real_time/threads:2$|HeightAtUncached|HeightAtCold|HeightAtDatum|EvaluateGroundReaders/readers:2|FillRasterSource/upsampler:0/threads:0"
    )
    set_tests_properties(benchmarks.smoke PROPERTIES LABELS benchmark)
    if(TARGET _terraingen)
        # The Python subclass benchmark on a tiny grid, to catch it no longer running.
        add_test(
            NAME benchmarks.python_subclasses_smoke
            COMMAND "${Python_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/bench_python_subclasses.py"
                    --size 16 --repeats 1)
        set_tests_properties(
            benchmarks.python_subclasses_smoke
            PROPERTIES ENVIRONMENT "PYTHONPATH=${PROJECT_BINARY_DIR}/python" LABELS benchmark)
    endif()
endif()
