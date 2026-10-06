include("${PROJECT_SOURCE_DIR}/cmake/third_party/benchmark.cmake")

# Timings are only meaningful from an optimized build: configure with -DCMAKE_BUILD_TYPE=Release.
add_executable(terraingen_benchmarks bench_craters.cpp bench_grids.cpp bench_point_queries.cpp)
target_include_directories(terraingen_benchmarks PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(terraingen_benchmarks PRIVATE terraingen::terraingen benchmark::benchmark
                                                    terraingen_warnings)

if(TERRAINGEN_BUILD_TESTS)
    # One iteration of the smallest case of each benchmark, to catch benchmarks that no longer run.
    add_test(
        NAME benchmarks.smoke
        COMMAND
            terraingen_benchmarks --benchmark_min_time=1x --benchmark_format=json
            "--benchmark_filter=GenerateCraters/256$|ApplyCraters/cells:1024/threads:0|EvaluateGrid/cells:512/threads:0|EvaluateTiles/level:5|EvaluateAtWarm/real_time/threads:2$|EvaluateAtUncached|EvaluateAtCold"
    )
    set_tests_properties(benchmarks.smoke PROPERTIES LABELS benchmark)
endif()
