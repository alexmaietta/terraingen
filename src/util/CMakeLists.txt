# SPDX-License-Identifier: BSD-3-Clause

find_package(Threads REQUIRED)
terraingen_add_library(
    util
    SOURCES
    checks.cpp
    fft.cpp
    log.cpp
    option_fields.cpp
    parallel.cpp
    special_functions.cpp
    text.cpp
    version.cpp
    PUBLIC
    Threads::Threads)
