find_package(Threads REQUIRED)
terraingen_add_library(
    util
    SOURCES
    checks.cpp
    cube_sphere_cells.cpp
    fft.cpp
    surface.cpp
    log.cpp
    parallel.cpp
    planar_cells.cpp
    special_functions.cpp
    version.cpp
    PUBLIC
    Threads::Threads)
