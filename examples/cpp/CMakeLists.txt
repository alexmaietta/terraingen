# The C++ examples, one executable each, linked against terraingen::terraingen as an application
# using an installed TerrainGen would be (find_package(terraingen), then the same link line).
foreach(
    example
    example_config_load
    example_terrain_fbm
    example_terrain_layered
    example_craters_simple
    example_craters_manual
    example_boulders_rocks
    example_albedo_crater_rays)
    add_executable(${example} ${example}.cpp)
    target_link_libraries(${example} PRIVATE terraingen::terraingen terraingen_warnings)
endforeach()
