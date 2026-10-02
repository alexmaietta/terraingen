set(TERRAINGEN_PACKAGE_MAINTAINER
    "TerrainGen developers"
    CACHE STRING "Maintainer recorded in generated packages, as \"Name <email>\".")

set(CPACK_PACKAGE_NAME terraingen)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Scientific terrain generation for the lunar surface")
set(CPACK_PACKAGE_CONTACT "${TERRAINGEN_PACKAGE_MAINTAINER}")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
set(CPACK_COMPONENTS_ALL runtime development)
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "TerrainGen command-line tool and terrain viewer")
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
set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME terraingen)
set(CPACK_DEBIAN_RUNTIME_PACKAGE_SECTION science)
set(CPACK_DEBIAN_RUNTIME_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_RUNTIME_PACKAGE_RECOMMENDS
    "libvulkan1, mesa-vulkan-drivers, libx11-6, libxrandr2, libxinerama1, libxcursor1, libxi6, libwayland-client0, libxkbcommon0"
)
set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_NAME terraingen-dev)
set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_SECTION libdevel)

include(CPack)
