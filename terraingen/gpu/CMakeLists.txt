# GPU backends and the choice between them. With TERRAINGEN_WITH_VULKAN, volk opens the Vulkan
# loader at run time, so one build runs with or without a GPU and links nothing of Vulkan.
if(TERRAINGEN_WITH_VULKAN)
    terraingen_add_library(
        gpu
        SOURCES
        backend.cpp
        vulkan.cpp
        PUBLIC
        terraingen_util
        PRIVATE
        terraingen_volk)
else()
    terraingen_add_library(gpu SOURCES backend.cpp vulkan_none.cpp PUBLIC terraingen_util)
endif()
