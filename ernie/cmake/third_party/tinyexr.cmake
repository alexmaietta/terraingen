# tinyexr (OpenEXR), vendored in third_party/tinyexr and compiled once, using stb's zlib instead of
# its bundled miniz.
add_library(ernie_tinyexr STATIC "${ERNIE_SOURCE_DIR}/third_party/tinyexr/tinyexr.cpp")
target_include_directories(ernie_tinyexr SYSTEM
                           PUBLIC "${ERNIE_SOURCE_DIR}/third_party/tinyexr")
target_compile_definitions(ernie_tinyexr PUBLIC TINYEXR_USE_MINIZ=0 TINYEXR_USE_STB_ZLIB=1
                                                     TINYEXR_USE_OPENMP=0)
target_link_libraries(ernie_tinyexr PUBLIC ernie_stb)
set_target_properties(ernie_tinyexr PROPERTIES POSITION_INDEPENDENT_CODE ON)
