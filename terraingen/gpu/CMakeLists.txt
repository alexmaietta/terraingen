# GPU backends and the choice between them. volk opens the Vulkan loader at run time, so one build
# runs with or without a GPU and links nothing of Vulkan.
terraingen_add_library(
    gpu
    SOURCES
    backend.cpp
    vulkan.cpp
    PUBLIC
    terraingen_util
    PRIVATE
    terraingen_volk)
