# GPU backends and the choice between them. volk opens the Vulkan loader at run time, so one build
# runs with or without a GPU and links nothing of Vulkan. Layers reach the device only through
# cell kernels; callers choose a backend.
terraingen_add_library(
    gpu
    SOURCES
    backend.cpp
    cell_kernels.cpp
    vulkan.cpp
    PUBLIC
    terraingen_raster
    terraingen_util
    PRIVATE
    terraingen_volk)
