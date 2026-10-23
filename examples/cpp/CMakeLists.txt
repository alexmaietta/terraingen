# SPDX-License-Identifier: BSD-3-Clause

# The C++ examples, each an `Example` (common/example.h) in an example_*.cpp in the category
# folders, linked against terraingen::terraingen as an application using an installed TerrainGen
# would be (find_package(terraingen), then the same link line).
#
# Each example compiles once, into an object library that both its own program
# (`example_<category>_<name>`, with common/standalone_main.cpp) and `terraingen-examples` (every
# example, with the GUI's Examples panel when the build has the GUI) link.
add_library(terraingen_examples_common STATIC common/example.cpp)
target_include_directories(terraingen_examples_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(terraingen_examples_common
                           PRIVATE TERRAINGEN_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
target_link_libraries(
    terraingen_examples_common
    PUBLIC terraingen::terraingen
    PRIVATE terraingen_warnings)

add_library(terraingen_examples_standalone_main OBJECT common/standalone_main.cpp)
target_link_libraries(terraingen_examples_standalone_main PRIVATE terraingen_examples_common
                                                                  terraingen_warnings)

add_executable(terraingen_examples common/main.cpp)
set_target_properties(terraingen_examples PROPERTIES OUTPUT_NAME terraingen-examples)
target_link_libraries(terraingen_examples PRIVATE terraingen_examples_common terraingen_warnings)
if(TERRAINGEN_GUI)
    target_sources(terraingen_examples PRIVATE common/browser.cpp)
    target_compile_definitions(terraingen_examples PRIVATE TERRAINGEN_EXAMPLES_GUI)
    target_link_libraries(terraingen_examples PRIVATE terraingen_gui)
endif()

file(GLOB_RECURSE examples CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/example_*.cpp")
foreach(source IN LISTS examples)
    get_filename_component(example "${source}" NAME_WE)
    add_library(${example}_object OBJECT "${source}")
    target_link_libraries(${example}_object PRIVATE terraingen_examples_common terraingen_warnings)
    add_executable(${example} $<TARGET_OBJECTS:${example}_object>
                              $<TARGET_OBJECTS:terraingen_examples_standalone_main>)
    target_link_libraries(${example} PRIVATE terraingen_examples_common)
    target_sources(terraingen_examples PRIVATE $<TARGET_OBJECTS:${example}_object>)
endforeach()
