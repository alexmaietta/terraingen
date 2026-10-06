set(package_dir "${PROJECT_BINARY_DIR}/python/terraingen")

nanobind_add_module(
    _terraingen
    STABLE_ABI
    NB_STATIC
    src/log.cpp
    src/module.cpp
    src/core.cpp
    src/kernels.cpp
    src/random.cpp)
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
set_property(GLOBAL APPEND PROPERTY TERRAINGEN_TEST_TARGETS _terraingen)

if(SKBUILD)
    install(TARGETS _terraingen LIBRARY DESTINATION terraingen)
endif()
