# The C++ examples, one executable per example_*.cpp, linked against terraingen::terraingen as an
# application using an installed TerrainGen would be (find_package(terraingen), then the same link
# line).
file(GLOB examples CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/example_*.cpp")
foreach(source IN LISTS examples)
    get_filename_component(example "${source}" NAME_WE)
    add_executable(${example} "${source}")
    target_link_libraries(${example} PRIVATE terraingen::terraingen terraingen_warnings)
endforeach()
