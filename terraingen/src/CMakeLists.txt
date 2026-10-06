configure_file("${PROJECT_SOURCE_DIR}/terraingen/include/terraingen/core/version.h.in"
               "${PROJECT_BINARY_DIR}/include/terraingen/core/version.h" @ONLY)

set(TERRAINGEN_BUILD_INFO_CPP "${CMAKE_CURRENT_BINARY_DIR}/build_info.cpp")

add_custom_target(
    terraingen_generate_build_info ALL
    COMMAND
        ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DTEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/core/build_info.cpp.in
        -DOUTPUT=${TERRAINGEN_BUILD_INFO_CPP} -P ${PROJECT_SOURCE_DIR}/cmake/GenerateBuildInfo.cmake
    BYPRODUCTS ${TERRAINGEN_BUILD_INFO_CPP}
    COMMENT "Regenerating TerrainGen build info (git SHA / build date)"
    VERBATIM)

add_library(terraingen_miniz OBJECT "${PROJECT_SOURCE_DIR}/third_party/miniz/miniz.c")
target_compile_definitions(
    terraingen_miniz PRIVATE MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS
                             MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
set_target_properties(terraingen_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(
    terraingen_core STATIC
    core/log.cpp
    core/version.cpp
    distributions/log_polynomial.cpp
    distributions/minimum.cpp
    distributions/piecewise_power_law.cpp
    distributions/power_law.cpp
    distributions/size_frequency.cpp
    distributions/spliced.cpp
    distributions/tabulated.cpp
    features/craters/ages/age_model.cpp
    features/craters/ages/fresh.cpp
    features/craters/ages/steady_state.cpp
    features/craters/ages/uniform.cpp
    features/craters/apply.cpp
    features/craters/degradation/freshness.cpp
    features/craters/degradation/none.cpp
    features/craters/degradation/state.cpp
    features/craters/filters.cpp
    features/craters/lunar/chronology.cpp
    features/craters/lunar/equilibrium.cpp
    features/craters/lunar/production.cpp
    features/craters/lunar/specifications.cpp
    features/craters/lunar/surface.cpp
    features/craters/population.cpp
    features/craters/profiles/fassett_thomson.cpp
    features/craters/profiles/martin_parkes_dunstan.cpp
    features/craters/rim_smoothing.cpp
    io/geotiff.cpp
    io/tiff_codecs.cpp
    random/samplers.cpp
    random/stream.cpp
    raster/filters.cpp
    raster/resample.cpp
    spatial/cells.cpp
    surfaces/synthetic_terrain.cpp
    "${TERRAINGEN_BUILD_INFO_CPP}"
    $<TARGET_OBJECTS:terraingen_miniz>)
add_library(terraingen::core ALIAS terraingen_core)
set_target_properties(terraingen_core PROPERTIES EXPORT_NAME core)
add_dependencies(terraingen_core terraingen_generate_build_info)

target_include_directories(
    terraingen_core
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/terraingen/include>
           $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(terraingen_core PUBLIC cxx_std_20)
target_link_libraries(terraingen_core PRIVATE $<BUILD_INTERFACE:terraingen_warnings>
                                              $<BUILD_INTERFACE:terraingen_coverage>)
target_include_directories(terraingen_core SYSTEM PRIVATE "${PROJECT_SOURCE_DIR}/third_party/miniz")
target_compile_definitions(
    terraingen_core PRIVATE MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS
                            MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/third_party")

add_library(terraingen_pipeline STATIC pipeline/config_json.cpp pipeline/run.cpp)
add_library(terraingen::pipeline ALIAS terraingen_pipeline)
set_target_properties(terraingen_pipeline PROPERTIES EXPORT_NAME pipeline)
target_link_libraries(
    terraingen_pipeline
    PUBLIC terraingen_core
    PRIVATE $<BUILD_INTERFACE:nlohmann_json> $<BUILD_INTERFACE:terraingen_warnings>
            $<BUILD_INTERFACE:terraingen_coverage>)

if(TERRAINGEN_INSTALL)
    install(
        TARGETS terraingen_core terraingen_pipeline
        EXPORT terraingenTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development)
    install(
        DIRECTORY "${PROJECT_SOURCE_DIR}/terraingen/include/terraingen"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT development
        PATTERN "*.in" EXCLUDE)
    install(
        FILES "${PROJECT_BINARY_DIR}/include/terraingen/core/version.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/terraingen/core
        COMPONENT development)
endif()
