# tinyexr (OpenEXR), vendored in third_party/tinyexr and compiled once, using stb's zlib instead of
# its bundled miniz.
add_library(terraingen_tinyexr STATIC "${PROJECT_SOURCE_DIR}/third_party/tinyexr/tinyexr.cpp")
target_include_directories(terraingen_tinyexr SYSTEM
                           PUBLIC "${PROJECT_SOURCE_DIR}/third_party/tinyexr")
target_compile_definitions(terraingen_tinyexr PUBLIC TINYEXR_USE_MINIZ=0 TINYEXR_USE_STB_ZLIB=1
                                                     TINYEXR_USE_OPENMP=0)
target_link_libraries(terraingen_tinyexr PUBLIC terraingen_stb)
set_target_properties(terraingen_tinyexr PROPERTIES POSITION_INDEPENDENT_CODE ON)
