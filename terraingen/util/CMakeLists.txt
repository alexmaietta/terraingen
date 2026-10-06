find_package(Threads REQUIRED)
terraingen_add_library(
    util
    SOURCES
    cells.cpp
    log.cpp
    parallel.cpp
    version.cpp
    PUBLIC
    Threads::Threads)
