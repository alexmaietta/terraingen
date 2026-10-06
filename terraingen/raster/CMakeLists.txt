# miniz provides Deflate for GeoTIFFs; its objects are compiled into this library.
add_library(terraingen_miniz OBJECT "${PROJECT_SOURCE_DIR}/third_party/miniz/miniz.c")
target_compile_definitions(
    terraingen_miniz PRIVATE MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS
                             MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
set_target_properties(terraingen_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

terraingen_add_library(
    raster
    SOURCES
    filters.cpp
    grid.cpp
    geotiff.cpp
    resample.cpp
    tiff_codecs.cpp
    $<TARGET_OBJECTS:terraingen_miniz>
    PUBLIC
    terraingen_util)
target_include_directories(terraingen_raster SYSTEM
                           PRIVATE "${PROJECT_SOURCE_DIR}/third_party/miniz")
target_compile_definitions(
    terraingen_raster PRIVATE MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS
                              MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
