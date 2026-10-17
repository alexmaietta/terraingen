# Compiling Slang kernels with slangc at build time and embedding them in libraries. Kernels
# include TerrainGen headers from the repository root, so kernel headers are shared with the C++
# code. Nothing of Slang ships or runs with TerrainGen.

# Which GPU forms of kernels the build makes besides SPIR-V (Vulkan), which it always makes:
#   TERRAINGEN_CUDA_KERNELS   PTX for CUDA: AUTO makes it when NVRTC (the CUDA toolkit) is found,
#                             ON requires it, OFF leaves CUDA out; the CUDA backend is unavailable
#                             without it, and running needs only the NVIDIA driver.
#   Metal source              on Apple platforms, where every machine has Metal.
set(TERRAINGEN_CUDA_KERNELS
    AUTO
    CACHE STRING "Compile kernels to PTX for the CUDA backend: AUTO, ON or OFF")
set_property(CACHE TERRAINGEN_CUDA_KERNELS PROPERTY STRINGS AUTO ON OFF)
set(TERRAINGEN_HAVE_CUDA_KERNELS OFF)
set(TERRAINGEN_SLANG_PTX_ENV)
if(NOT TERRAINGEN_CUDA_KERNELS STREQUAL "OFF")
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND AND TARGET CUDA::nvrtc)
        set(TERRAINGEN_HAVE_CUDA_KERNELS ON)
        # slangc loads NVRTC by name, so the toolkit's library folder goes on the search path.
        if(WIN32)
            set(TERRAINGEN_SLANG_PTX_ENV "PATH=${CUDAToolkit_BIN_DIR};$ENV{PATH}")
        else()
            set(TERRAINGEN_SLANG_PTX_ENV
                "LD_LIBRARY_PATH=${CUDAToolkit_LIBRARY_DIR}:$ENV{LD_LIBRARY_PATH}")
        endif()
        message(STATUS "CUDA kernels: compiled to PTX with NVRTC ${CUDAToolkit_VERSION}")
    elseif(TERRAINGEN_CUDA_KERNELS STREQUAL "ON")
        message(
            FATAL_ERROR "TERRAINGEN_CUDA_KERNELS is ON but NVRTC (the CUDA toolkit) is not found")
    else()
        message(STATUS "CUDA kernels: off (NVRTC not found)")
    endif()
endif()
set(TERRAINGEN_HAVE_METAL_KERNELS ${APPLE})

# terraingen_compile_slang(<target> <symbol> <source> <slang target> <extension> [FLAGS ...]
# [ENV ...]): compiles the Slang compute kernel <source> (entry point "main") for <slang target>
# and adds it to <target> as the byte array `<symbol>` and its size `<symbol>_size`, both with C
# linkage. FLAGS go to slangc and ENV sets its environment.
function(terraingen_compile_slang target symbol source slang_target extension)
    cmake_parse_arguments(PARSE_ARGV 5 ARG "" "" "FLAGS;ENV")
    get_filename_component(source "${source}" ABSOLUTE)
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/kernels")
    set(output "${directory}/${symbol}.${extension}")
    set(embedded "${directory}/${symbol}.cpp")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${directory}"
        COMMAND
            ${CMAKE_COMMAND} -E env ${ARG_ENV} "${TERRAINGEN_SLANGC}" "${source}" -target
            ${slang_target} ${ARG_FLAGS} -stage compute -entry main -O2 -I "${PROJECT_SOURCE_DIR}"
            -o "${output}" -depfile "${output}.d"
        DEPENDS "${source}"
        DEPFILE "${output}.d"
        COMMENT "Compiling the Slang kernel ${symbol}"
        VERBATIM)
    terraingen_embed_kernel(${target} ${symbol} "${output}")
endfunction()

# terraingen_embed_kernel(<target> <symbol> <file>): adds the bytes of <file> to <target> as
# `<symbol>` and `<symbol>_size` (cmake/EmbedFile.cmake).
function(terraingen_embed_kernel target symbol file)
    set(embedded "${CMAKE_CURRENT_BINARY_DIR}/kernels/${symbol}.cpp")
    add_custom_command(
        OUTPUT "${embedded}"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${file} -DOUTPUT=${embedded} -DSYMBOL=${symbol} -P
                "${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake"
        DEPENDS "${file}" "${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake"
        COMMENT "Embedding the kernel ${symbol}"
        VERBATIM)
    target_sources(${target} PRIVATE "${embedded}")
endfunction()

# terraingen_add_slang_kernel(<target> <symbol> <source>): compiles the Slang compute kernel
# <source> (entry point "main") to SPIR-V and adds it to <target> as `<symbol>` and
# `<symbol>_size`.
function(terraingen_add_slang_kernel target symbol source)
    terraingen_compile_slang(
        ${target}
        ${symbol}
        "${source}"
        spirv
        spv
        FLAGS
        -profile
        spirv_1_3)
endfunction()

# terraingen_add_kernel(<target> <symbol> <type> <header>): compiles the kernel terraingen::<type>
# for every GPU backend the build supports and defines `CompiledKernel<type>()` in <target>. <header>,
# relative to the repository root, is a kernel header (see util/portable.h) declaring the plain
# struct <type>, `float ApplyCell(<type> kernel, float height, CellPosition position)` and the
# `CompiledKernel` specialization; the generated entry point (cmake/Kernel.slang.in) applies it to grid
# cells for RunKernel. A form the build cannot make is embedded empty.
function(terraingen_add_kernel target symbol type header)
    set(KERNEL_TYPE "${type}")
    set(KERNEL_HEADER "${header}")
    set(KERNEL_SYMBOL "${symbol}")
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/kernels")
    set(source "${directory}/${symbol}.slang")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/Kernel.slang.in" "${source}" @ONLY)
    configure_file("${PROJECT_SOURCE_DIR}/cmake/Kernel.cpp.in" "${directory}/${symbol}_program.cpp"
                   @ONLY)
    target_sources(${target} PRIVATE "${directory}/${symbol}_program.cpp")
    set(empty "${directory}/empty")
    file(WRITE "${empty}" "")
    terraingen_compile_slang(
        ${target}
        ${symbol}_spirv
        "${source}"
        spirv
        spv
        FLAGS
        -profile
        spirv_1_3)
    if(TERRAINGEN_HAVE_CUDA_KERNELS)
        terraingen_compile_slang(
            ${target}
            ${symbol}_ptx
            "${source}"
            ptx
            ptx
            FLAGS
            -warnings-disable
            40100
            ENV
            "${TERRAINGEN_SLANG_PTX_ENV}")
    else()
        terraingen_embed_kernel(${target} ${symbol}_ptx "${empty}")
    endif()
    if(TERRAINGEN_HAVE_METAL_KERNELS)
        terraingen_compile_slang(
            ${target}
            ${symbol}_metal
            "${source}"
            metal
            metal
            FLAGS
            -warnings-disable
            40100)
    else()
        terraingen_embed_kernel(${target} ${symbol}_metal "${empty}")
    endif()
endfunction()
