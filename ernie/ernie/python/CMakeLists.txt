set(ernie_package_dir "${CMAKE_BINARY_DIR}/python/ernie")

nanobind_add_module(
    _ernie
    STABLE_ABI
    NB_STATIC
    src/module.cpp
    src/render.cpp
    src/scene.cpp
    src/tensor.cpp)
target_link_libraries(_ernie PRIVATE ernie ernie_build_options)
if(TARGET ernie_gui)
    target_sources(_ernie PRIVATE src/gui.cpp)
    target_link_libraries(_ernie PRIVATE ernie_gui)
    target_compile_definitions(_ernie PRIVATE ERNIE_HAS_GUI)
endif()
target_compile_features(_ernie PRIVATE cxx_std_20)
target_include_directories(_ernie SYSTEM PRIVATE "${NB_DIR}/include" ${Python_INCLUDE_DIRS})
foreach(nanobind_library nanobind-static nanobind-static-abi3)
    if(TARGET ${nanobind_library})
        set_target_properties(${nanobind_library} PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
    endif()
endforeach()
set_target_properties(_ernie PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${ernie_package_dir}")
configure_file(ernie/__init__.py "${ernie_package_dir}/__init__.py" COPYONLY)
configure_file(ernie/gui.py "${ernie_package_dir}/gui.py" COPYONLY)
if(SKBUILD)
    install(TARGETS _ernie LIBRARY DESTINATION ernie)
endif()
