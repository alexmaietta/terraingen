# miniz provides Deflate; its objects are compiled into this library.
add_library(terraingen_miniz OBJECT "${PROJECT_SOURCE_DIR}/third_party/miniz/miniz.c")
target_compile_definitions(
    terraingen_miniz PRIVATE MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS
                             MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
set_target_properties(terraingen_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

terraingen_add_library(
    io
    SOURCES
    geotiff.cpp
    tiff_codecs.cpp
    $<TARGET_OBJECTS:terraingen_miniz>
    PUBLIC
    terraingen_core
    terraingen_raster)
target_include_directories(terraingen_io SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/miniz")
target_compile_definitions(terraingen_io PRIVATE MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS
                                                 MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
