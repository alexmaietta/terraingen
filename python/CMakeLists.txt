# SPDX-License-Identifier: BSD-3-Clause

set(package_dir "${PROJECT_BINARY_DIR}/python/terraingen")

nanobind_add_module(
    _terraingen
    STABLE_ABI
    NB_STATIC
    src/module.cpp
    src/util.cpp
    src/raster.cpp
    src/noise.cpp
    src/random.cpp
    src/gpu.cpp
    src/mesh.cpp
    src/distribution.cpp
    src/crater_models.cpp
    src/chain_seams.cpp
    src/sources.cpp
    src/layers.cpp
    src/craters.cpp
    src/boulders.cpp
    src/albedo.cpp
    src/materials.cpp
    src/pipeline.cpp
    src/report.cpp)
target_link_libraries(_terraingen PRIVATE terraingen::terraingen terraingen_internal
                                          terraingen_warnings terraingen_coverage)
target_include_directories(_terraingen SYSTEM PRIVATE "${NB_DIR}/include" ${Python_INCLUDE_DIRS})
foreach(nanobind_library nanobind-static nanobind-static-abi3)
    if(TARGET ${nanobind_library})
        set_target_properties(${nanobind_library} PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
        # GCC 11 miscompiles nanobind's lock-free override cache, so overrides read a bad name.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 12)
            set_source_files_properties("${NB_DIR}/src/trampoline.cpp" TARGET_DIRECTORY
                                        ${nanobind_library} PROPERTIES COMPILE_OPTIONS -O1)
        endif()
    endif()
endforeach()
# The generator expression keeps multi-config generators (Visual Studio) from adding a Release/
# folder, so the stub generation and the package find the module beside __init__.py.
set_target_properties(_terraingen PROPERTIES LIBRARY_OUTPUT_DIRECTORY "$<1:${package_dir}>")

configure_file(terraingen/__init__.py "${package_dir}/__init__.py" COPYONLY)

# Type stubs of the extension (signatures and docstrings for editors and type checkers), and the
# py.typed marker that says the package has them, made from the built module after each build.
# The module is imported alone, so generating them needs neither the package nor numpy.
nanobind_add_stub(
    _terraingen_stub
    MODULE
    _terraingen
    OUTPUT
    "${package_dir}/_terraingen.pyi"
    MARKER_FILE
    "${package_dir}/py.typed"
    PYTHON_PATH
    "${package_dir}"
    DEPENDS
    _terraingen)
set_property(GLOBAL APPEND PROPERTY TERRAINGEN_TEST_TARGETS _terraingen _terraingen_stub)

if(SKBUILD)
    install(TARGETS _terraingen LIBRARY DESTINATION terraingen)
    install(FILES "${package_dir}/_terraingen.pyi" "${package_dir}/py.typed" DESTINATION terraingen)
endif()
