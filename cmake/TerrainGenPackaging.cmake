set(TERRAINGEN_PACKAGE_MAINTAINER
    "TerrainGen developers"
    CACHE STRING "Maintainer recorded in generated packages, as \"Name <email>\".")

set(CPACK_PACKAGE_NAME terraingen)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Scientific terrain generation for the lunar surface")
set(CPACK_PACKAGE_CONTACT "${TERRAINGEN_PACKAGE_MAINTAINER}")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_COMPONENTS_ALL development)
set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION
    "TerrainGen headers, static library and CMake package for building against TerrainGen")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CPACK_GENERATOR DEB)
else()
    set(CPACK_GENERATOR TGZ)
endif()

set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${TERRAINGEN_PACKAGE_MAINTAINER}")
set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_NAME terraingen-dev)
set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_SECTION libdevel)

include(CPack)
