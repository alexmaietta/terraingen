set(TERRAINGEN_DEP_VERSION_benchmark
    v1.9.5
    CACHE STRING "Tag of Google Benchmark to fetch.")
mark_as_advanced(TERRAINGEN_DEP_VERSION_benchmark)

include(FetchContent)

set(BENCHMARK_ENABLE_TESTING OFF)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF)
set(BENCHMARK_ENABLE_INSTALL OFF)
set(BENCHMARK_INSTALL_DOCS OFF)

FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG ${TERRAINGEN_DEP_VERSION_benchmark}
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(benchmark)
