# GPU backends and the choice between them. Each GPU library is opened at run time (the Vulkan
# loader through volk, the NVIDIA driver for CUDA) or is part of the system (Metal on Apple
# platforms), so one build runs with or without a GPU and links nothing of Vulkan or CUDA. Layers
# reach the devices only through kernels; callers choose a backend.
terraingen_add_library(
    gpu
    SOURCES
    backend.cpp
    cuda.cpp
    kernel.cpp
    metal.cpp
    vulkan.cpp
    PUBLIC
    terraingen_raster
    terraingen_util
    PRIVATE
    terraingen_volk
    ${CMAKE_DL_LIBS})
target_compile_definitions(
    terraingen_gpu PRIVATE TERRAINGEN_HAS_CUDA_KERNELS=$<BOOL:${TERRAINGEN_HAVE_CUDA_KERNELS}>
                           TERRAINGEN_HAS_METAL_KERNELS=$<BOOL:${TERRAINGEN_HAVE_METAL_KERNELS}>)
if(APPLE)
    set_source_files_properties(metal.cpp PROPERTIES LANGUAGE OBJCXX COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(terraingen_gpu PRIVATE "-framework Metal" "-framework Foundation")
endif()
