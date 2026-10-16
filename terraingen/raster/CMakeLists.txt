# miniz provides Deflate for GeoTIFFs; its objects are compiled into this library. Its
# configuration and headers sit on one interface target that both miniz and the raster use.
add_library(terraingen_miniz_config INTERFACE)
target_compile_definitions(
    terraingen_miniz_config INTERFACE MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS
                                      MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
target_include_directories(terraingen_miniz_config SYSTEM
                           INTERFACE "${PROJECT_SOURCE_DIR}/third_party/miniz")

add_library(terraingen_miniz OBJECT "${PROJECT_SOURCE_DIR}/third_party/miniz/miniz.c")
target_link_libraries(terraingen_miniz PRIVATE terraingen_miniz_config)
set_target_properties(terraingen_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

terraingen_add_library(
    raster
    SOURCES
    cell_loops.cpp
    filters.cpp
    grid.cpp
    geotiff.cpp
    geotiff_read.cpp
    geotiff_write.cpp
    resample.cpp
    tiff_codecs.cpp
    $<TARGET_OBJECTS:terraingen_miniz>
    PUBLIC
    terraingen_util
    PRIVATE
    $<BUILD_INTERFACE:terraingen_miniz_config>)
