# SPDX-License-Identifier: BSD-3-Clause

terraingen_add_library(
    random
    SOURCES
    samplers.cpp
    stream.cpp
    value_distribution.cpp
    PUBLIC
    terraingen_util)
