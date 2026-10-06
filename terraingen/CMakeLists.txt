# TerrainGen's modules, one library each, every module depending only on the ones before it:
#   core           logging, version and the build info's header
#   raster         rasters, geotransforms, filters and resampling
#   noise          noise kernels (header-only, GPU-shareable) and synthetic terrain
#   random         counter-based random streams and samplers
#   distributions  crater size-frequency distributions
#   features       per-cell feature placement and craters
#   io             GeoTIFF reading and writing
#   pipeline       JSON terrain configs and the generation pipeline
# Headers sit next to their sources and are included from the repository root as
# "terraingen/<module>/<file>.h"; terraingen::terraingen links every module and the build info.

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/core/version.h.in"
               "${PROJECT_BINARY_DIR}/include/terraingen/core/version.h" @ONLY)

# terraingen_add_library(<module> SOURCES ... [PUBLIC ...] [PRIVATE ...]): the static library
# terraingen_<module>, exported as terraingen::<module>.
function(terraingen_add_library module)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;PUBLIC;PRIVATE")
    set(target terraingen_${module})
    add_library(${target} STATIC ${ARG_SOURCES})
    add_library(terraingen::${module} ALIAS ${target})
    set_target_properties(${target} PROPERTIES EXPORT_NAME ${module})
    if(TERRAINGEN_BUILD_PYTHON)
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    endif()
    target_include_directories(
        ${target}
        PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
               $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
               $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_link_libraries(
        ${target}
        PUBLIC ${ARG_PUBLIC}
        PRIVATE ${ARG_PRIVATE} $<BUILD_INTERFACE:terraingen_warnings>
                $<BUILD_INTERFACE:terraingen_coverage>)
    set_property(GLOBAL APPEND PROPERTY TERRAINGEN_MODULES ${target})
endfunction()

add_subdirectory(core)
add_subdirectory(raster)
add_subdirectory(noise)
add_subdirectory(random)
add_subdirectory(distributions)
add_subdirectory(features)
add_subdirectory(io)
add_subdirectory(pipeline)

get_property(modules GLOBAL PROPERTY TERRAINGEN_MODULES)

# The build info sits above every module: it is regenerated, with a fresh build time, only when a
# module's library or the git state changes, so builds without changes relink nothing.
set(git_state "${CMAKE_CURRENT_BINARY_DIR}/git_state.txt")
set(build_info "${CMAKE_CURRENT_BINARY_DIR}/build_info.cpp")
add_custom_target(
    terraingen_git_state ALL
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DOUTPUT=${git_state}
            -DMODE=git_state -P ${PROJECT_SOURCE_DIR}/cmake/BuildInfo.cmake
    BYPRODUCTS ${git_state}
    COMMENT "Checking the TerrainGen git state"
    VERBATIM)
set(module_files)
foreach(module IN LISTS modules)
    list(APPEND module_files $<TARGET_FILE:${module}>)
endforeach()
add_custom_command(
    OUTPUT ${build_info}
    COMMAND
        ${CMAKE_COMMAND} -DMODE=source -DSTATE=${git_state}
        -DTEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/core/build_info.cpp.in -DOUTPUT=${build_info} -P
        ${PROJECT_SOURCE_DIR}/cmake/BuildInfo.cmake
    DEPENDS ${git_state} ${CMAKE_CURRENT_SOURCE_DIR}/core/build_info.cpp.in
            ${PROJECT_SOURCE_DIR}/cmake/BuildInfo.cmake ${module_files}
    COMMENT "Regenerating TerrainGen build info (git SHA / build time)"
    VERBATIM)
terraingen_add_library(build_info SOURCES ${build_info} PUBLIC terraingen_core)
add_dependencies(terraingen_build_info terraingen_git_state ${modules})

add_library(terraingen INTERFACE)
add_library(terraingen::terraingen ALIAS terraingen)
target_link_libraries(terraingen INTERFACE ${modules} terraingen_build_info)

if(TERRAINGEN_BUILD_PYTHON)
    add_subdirectory(python)
endif()

if(TERRAINGEN_INSTALL)
    install(
        TARGETS ${modules} terraingen_build_info terraingen
        EXPORT terraingenTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development)
    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/terraingen
        COMPONENT development
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "python" EXCLUDE)
    install(
        FILES "${PROJECT_BINARY_DIR}/include/terraingen/core/version.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/terraingen/core
        COMPONENT development)
endif()
