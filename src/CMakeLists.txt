configure_file("${PROJECT_SOURCE_DIR}/include/terraingen/version.h.in"
               "${PROJECT_BINARY_DIR}/include/terraingen/version.h" @ONLY)

set(TERRAINGEN_BUILD_INFO_CPP "${PROJECT_BINARY_DIR}/src/build_info.cpp")

add_custom_target(
    terraingen_generate_build_info ALL
    COMMAND
        ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DTEMPLATE=${PROJECT_SOURCE_DIR}/src/build_info.cpp.in -DOUTPUT=${TERRAINGEN_BUILD_INFO_CPP}
        -P ${PROJECT_SOURCE_DIR}/cmake/GenerateBuildInfo.cmake
    BYPRODUCTS ${TERRAINGEN_BUILD_INFO_CPP}
    COMMENT "Regenerating TerrainGen build info (git SHA / build date)"
    VERBATIM)

add_library(terraingen_core STATIC version.cpp "${TERRAINGEN_BUILD_INFO_CPP}")
add_library(terraingen::core ALIAS terraingen_core)
set_target_properties(terraingen_core PROPERTIES EXPORT_NAME core)
add_dependencies(terraingen_core terraingen_generate_build_info)

target_include_directories(
    terraingen_core
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
           $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(terraingen_core PUBLIC cxx_std_20)
target_link_libraries(terraingen_core PRIVATE $<BUILD_INTERFACE:terraingen_warnings>)

if(TERRAINGEN_INSTALL)
    install(
        TARGETS terraingen_core
        EXPORT terraingenTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(
        DIRECTORY "${PROJECT_SOURCE_DIR}/include/terraingen"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "*.in" EXCLUDE)
    install(FILES "${PROJECT_BINARY_DIR}/include/terraingen/version.h"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/terraingen)
endif()
