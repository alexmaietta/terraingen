cmake_minimum_required(VERSION 3.22)

project(TerrainGenPackageTest LANGUAGES CXX)

find_package(terraingen 0.2 REQUIRED)

add_executable(package_test main.cpp)
target_link_libraries(package_test PRIVATE terraingen::terraingen)
