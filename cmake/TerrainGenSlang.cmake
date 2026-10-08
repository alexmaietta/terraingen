# terraingen_add_slang_kernel(<target> <symbol> <source>): compiles the Slang compute kernel
# <source> (entry point "main") to SPIR-V with slangc and adds it to <target> as the byte array
# `<symbol>` and its size `<symbol>_size`, both with C linkage. Kernels include TerrainGen headers
# from the repository root, so kernel headers are shared with the C++ code.
function(terraingen_add_slang_kernel target symbol source)
    get_filename_component(source "${source}" ABSOLUTE)
    set(spirv "${CMAKE_CURRENT_BINARY_DIR}/kernels/${symbol}.spv")
    set(depfile "${CMAKE_CURRENT_BINARY_DIR}/kernels/${symbol}.spv.d")
    set(embedded "${CMAKE_CURRENT_BINARY_DIR}/kernels/${symbol}.cpp")
    add_custom_command(
        OUTPUT "${spirv}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/kernels"
        COMMAND "${TERRAINGEN_SLANGC}" "${source}" -target spirv -profile spirv_1_3 -stage compute
                -entry main -O2 -I "${PROJECT_SOURCE_DIR}" -o "${spirv}" -depfile "${depfile}"
        DEPENDS "${source}"
        DEPFILE "${depfile}"
        COMMENT "Compiling the Slang kernel ${symbol}"
        VERBATIM)
    add_custom_command(
        OUTPUT "${embedded}"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${spirv} -DOUTPUT=${embedded} -DSYMBOL=${symbol} -P
                "${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake"
        DEPENDS "${spirv}" "${PROJECT_SOURCE_DIR}/cmake/EmbedFile.cmake"
        COMMENT "Embedding the Slang kernel ${symbol}"
        VERBATIM)
    target_sources(${target} PRIVATE "${embedded}")
endfunction()
