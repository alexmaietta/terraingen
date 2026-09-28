configure_file("${CMAKE_SOURCE_DIR}/include/terraingen/version.h.in"
               "${CMAKE_BINARY_DIR}/include/terraingen/version.h" @ONLY)

set(TERRAINGEN_BUILD_INFO_CPP "${CMAKE_BINARY_DIR}/src/build_info.cpp")

add_custom_target(
    generate_build_info ALL
    COMMAND
        ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DTEMPLATE=${CMAKE_SOURCE_DIR}/src/build_info.cpp.in -DOUTPUT=${TERRAINGEN_BUILD_INFO_CPP}
        -P ${CMAKE_SOURCE_DIR}/cmake/GenerateBuildInfo.cmake
    BYPRODUCTS ${TERRAINGEN_BUILD_INFO_CPP}
    COMMENT "Regenerating TerrainGen build info (git SHA / build date)"
    VERBATIM)

add_library(terraingen STATIC version.cpp "${TERRAINGEN_BUILD_INFO_CPP}")
add_library(terraingen::terraingen ALIAS terraingen)
add_dependencies(terraingen generate_build_info)

target_include_directories(terraingen PUBLIC "${CMAKE_SOURCE_DIR}/include"
                                             "${CMAKE_BINARY_DIR}/include")

install(
    TARGETS terraingen
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(
    DIRECTORY "${CMAKE_SOURCE_DIR}/include/terraingen"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN "*.in" EXCLUDE)
install(FILES "${CMAKE_BINARY_DIR}/include/terraingen/version.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/terraingen)
