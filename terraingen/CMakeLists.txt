# TerrainGen's modules, one library each, every module depending only on the ones before it:
#   util          logging, version, build info header, geometry and surface cells
#   raster        rasters, geotransforms, interpolation and GeoTIFF files in geotiff/
#   noise         noise kernels, header-only and portable to GPU shading languages, and their checks
#   gpu           backend selection and the Vulkan, CUDA and Metal devices, opened at run time
#   random        the Philox generator (portable to GPUs), random streams and samplers
#   mesh          triangle meshes, base solids, subdivision and mesh files
#   distribution  size-frequency distributions
#   sources       where heights come from: rasters, GeoTIFFs, chains and noise, and their upsampling
#   layers        relief added on top of a source: craters (populations, profiles, degradation)
#   albedo        surface albedo: orbital maps upscaled, synthetic color and bright crater rays
#   pipeline      terrains evaluated over grids, tiles, points and whole-body meshes
#   config        JSON terrain configs: parsing, validation, building and writing outputs
# Headers sit next to their sources and are included from the repository root as
# "terraingen/<module>/<file>.h"; terraingen::terraingen links every module and the build info.

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/util/version.h.in"
               "${PROJECT_BINARY_DIR}/include/terraingen/util/version.h" @ONLY)

# terraingen_add_library(<module> [SOURCES ...] [PUBLIC ...] [PRIVATE ...]): the library
# terraingen_<module>, exported as terraingen::<module>; static, or an interface library for a
# header-only module without SOURCES.
function(terraingen_add_library module)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;PUBLIC;PRIVATE")
    set(target terraingen_${module})
    if(ARG_SOURCES)
        add_library(${target} STATIC ${ARG_SOURCES})
        set(scope PUBLIC)
        target_link_libraries(
            ${target} PRIVATE ${ARG_PRIVATE} $<BUILD_INTERFACE:terraingen_warnings>
                              $<BUILD_INTERFACE:terraingen_coverage>)
        set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        set_property(GLOBAL APPEND PROPERTY TERRAINGEN_STATIC_MODULES ${target})
    else()
        add_library(${target} INTERFACE)
        set(scope INTERFACE)
    endif()
    add_library(terraingen::${module} ALIAS ${target})
    set_target_properties(${target} PROPERTIES EXPORT_NAME ${module})
    target_include_directories(
        ${target} ${scope} $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_features(${target} ${scope} cxx_std_17)
    target_link_libraries(${target} ${scope} ${ARG_PUBLIC})
    set_property(GLOBAL APPEND PROPERTY TERRAINGEN_MODULES ${target})
endfunction()

add_subdirectory(util)
add_subdirectory(raster)
add_subdirectory(noise)
add_subdirectory(gpu)
add_subdirectory(random)
add_subdirectory(mesh)
add_subdirectory(distribution)
add_subdirectory(sources)
add_subdirectory(layers)
add_subdirectory(albedo)
add_subdirectory(pipeline)
add_subdirectory(config)

get_property(modules GLOBAL PROPERTY TERRAINGEN_MODULES)
get_property(static_modules GLOBAL PROPERTY TERRAINGEN_STATIC_MODULES)

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
foreach(module IN LISTS static_modules)
    list(APPEND module_files $<TARGET_FILE:${module}>)
endforeach()
add_custom_command(
    OUTPUT ${build_info}
    COMMAND
        ${CMAKE_COMMAND} -DMODE=source -DSTATE=${git_state}
        -DTEMPLATE=${CMAKE_CURRENT_SOURCE_DIR}/util/build_info.cpp.in -DOUTPUT=${build_info} -P
        ${PROJECT_SOURCE_DIR}/cmake/BuildInfo.cmake
    DEPENDS ${git_state} ${CMAKE_CURRENT_SOURCE_DIR}/util/build_info.cpp.in
            ${PROJECT_SOURCE_DIR}/cmake/BuildInfo.cmake ${module_files}
    COMMENT "Regenerating TerrainGen build info (git SHA / build time)"
    VERBATIM)
terraingen_add_library(build_info SOURCES ${build_info} PUBLIC terraingen_util)
add_dependencies(terraingen_build_info terraingen_git_state ${static_modules})

add_library(terraingen INTERFACE)
add_library(terraingen::terraingen ALIAS terraingen)
target_link_libraries(terraingen INTERFACE ${modules} terraingen_build_info)

if(TERRAINGEN_BUILD_PYTHON)
    add_subdirectory(python)
endif()

if(TERRAINGEN_BUILD_APP)
    add_subdirectory(app)
endif()

if(TERRAINGEN_INSTALL)
    install(
        TARGETS ${modules} terraingen_build_info terraingen terraingen_volk
                terraingen_vulkan_headers
        EXPORT terraingenTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT development)
    install(
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/terraingen
        COMPONENT development
        FILES_MATCHING
        PATTERN "*.h"
        PATTERN "python" EXCLUDE
        PATTERN "app" EXCLUDE)
    install(
        FILES "${PROJECT_BINARY_DIR}/include/terraingen/util/version.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/terraingen/util
        COMPONENT development)
endif()
