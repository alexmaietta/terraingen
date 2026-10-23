# SPDX-License-Identifier: BSD-3-Clause

# The kernels are header-only: they compile into whatever includes them, on the CPU or ported to
# GPUs. The library itself holds the host-side parameter checks.
terraingen_add_library(
    noise
    SOURCES
    structure_function.cpp
    surface_noise.cpp
    validate.cpp
    PUBLIC
    terraingen_util)
