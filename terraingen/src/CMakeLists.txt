configure_file("${PROJECT_SOURCE_DIR}/terraingen/include/terraingen/core/version.h.in"
               "${PROJECT_BINARY_DIR}/include/terraingen/core/version.h" @ONLY)

add_library(terraingen_miniz OBJECT "${PROJECT_SOURCE_DIR}/third_party/miniz/miniz.c")
target_compile_definitions(
    terraingen_miniz PRIVATE MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS
                             MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
set_target_properties(terraingen_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The library's sources compile into an object library so the build info can depend on their
# objects: it is regenerated, with a fresh build time, only when an object or the git state
# changes, and builds without changes relink nothing.
add_library(
    terraingen_core_objects OBJECT
    core/log.cpp
    core/version.cpp
    distributions/log_polynomial.cpp
    distributions/minimum.cpp
    distributions/piecewise_power_law.cpp
    distributions/power_law.cpp
    distributions/size_frequency.cpp
    distributions/spliced.cpp
    distributions/tabulated.cpp
    features/cells.cpp
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
    noise/synthetic_terrain.cpp)
target_include_directories(terraingen_core_objects PUBLIC "${PROJECT_SOURCE_DIR}/terraingen/include"
                                                          "${PROJECT_BINARY_DIR}/include")
target_include_directories(terraingen_core_objects SYSTEM
                           PRIVATE "${PROJECT_SOURCE_DIR}/third_party/miniz")
target_compile_features(terraingen_core_objects PUBLIC cxx_std_20)
target_compile_definitions(
    terraingen_core_objects PRIVATE MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS
                                    MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
target_link_libraries(terraingen_core_objects PRIVATE terraingen_warnings terraingen_coverage)

set(git_state "${CMAKE_CURRENT_BINARY_DIR}/git_state.txt")
set(build_info "${CMAKE_CURRENT_BINARY_DIR}/build_info.cpp")
add_custom_target(
    terraingen_git_state ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DOUTPUT=${git_state}
            -DMODE=git_state -P ${PROJECT_SOURCE_DIR}/cmake/BuildInfo.cmake
    BYPRODUCTS ${git_state}
    COMMENT "Checking the TerrainGen git state"
    VERBATIM)
add_custom_command(
    OUTPUT ${build_info}
    COMMAND
        ${CMAKE_COMMAND} -DMODE=source -DSTATE=${git_state}
        -DTEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/core/build_info.cpp.in -DOUTPUT=${build_info} -P
        ${PROJECT_SOURCE_DIR}/cmake/BuildInfo.cmake
    DEPENDS ${git_state} ${CMAKE_CURRENT_SOURCE_DIR}/core/build_info.cpp.in
            ${PROJECT_SOURCE_DIR}/cmake/BuildInfo.cmake $<TARGET_OBJECTS:terraingen_core_objects>
    COMMENT "Regenerating TerrainGen build info (git SHA / build time)"
    VERBATIM)

add_library(terraingen_core STATIC ${build_info} $<TARGET_OBJECTS:terraingen_core_objects>
                                   $<TARGET_OBJECTS:terraingen_miniz>)
add_library(terraingen::core ALIAS terraingen_core)
set_target_properties(terraingen_core PROPERTIES EXPORT_NAME core)
add_dependencies(terraingen_core terraingen_git_state terraingen_core_objects)

target_include_directories(
    terraingen_core
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/terraingen/include>
           $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(terraingen_core PUBLIC cxx_std_20)
target_link_libraries(terraingen_core PRIVATE $<BUILD_INTERFACE:terraingen_warnings>
                                              $<BUILD_INTERFACE:terraingen_coverage>)

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
