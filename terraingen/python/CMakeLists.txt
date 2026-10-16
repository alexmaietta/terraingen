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
    src/meshes.cpp
    src/distribution.cpp
    src/crater_models.cpp
    src/sources.cpp
    src/layers.cpp
    src/craters.cpp
    src/boulders.cpp
    src/filters.cpp
    src/albedo.cpp
    src/bodies.cpp
    src/pipeline.cpp)
target_link_libraries(_terraingen PRIVATE terraingen::terraingen terraingen_warnings
                                          terraingen_coverage)
target_include_directories(_terraingen SYSTEM PRIVATE "${NB_DIR}/include" ${Python_INCLUDE_DIRS})
foreach(nanobind_library nanobind-static nanobind-static-abi3)
    if(TARGET ${nanobind_library})
        set_target_properties(${nanobind_library} PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
    endif()
endforeach()
set_target_properties(_terraingen PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${package_dir}")

configure_file(terraingen/__init__.py "${package_dir}/__init__.py" COPYONLY)
configure_file(terraingen/render.py "${package_dir}/render.py" COPYONLY)

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
    # The example configurations, ready for `terraingen CONFIG --output-dir DIR`.
    install(DIRECTORY "${PROJECT_SOURCE_DIR}/examples/config/"
            DESTINATION terraingen/examples/config)
endif()
