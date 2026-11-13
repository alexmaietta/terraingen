# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.22)

project(TerrainGenPackageTest LANGUAGES CXX)

# The minor version under test, given by the build that runs this project.
find_package(terraingen ${TERRAINGEN_REQUIRED_VERSION} REQUIRED)

add_executable(package_test main.cpp)
target_link_libraries(package_test PRIVATE terraingen::terraingen)
