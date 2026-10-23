# The kernels are header-only: they compile into whatever includes them, on the CPU or ported to
# GPUs. The library itself holds the host-side parameter checks.
terraingen_add_library(noise SOURCES surface_noise.cpp validate.cpp PUBLIC terraingen_util)
