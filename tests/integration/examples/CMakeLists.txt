# Runs every example: each config through the terraingen command, each C++ example, and each
# Python example when the bindings and NumPy are available. Outputs go under the build tree.
set(examples_dir "${PROJECT_SOURCE_DIR}/examples")
set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/output")

if(TARGET terraingen_app)
    file(GLOB configs CONFIGURE_DEPENDS "${examples_dir}/config/example_*.json")
    foreach(config IN LISTS configs)
        get_filename_component(name "${config}" NAME_WE)
        add_test(NAME examples.config.${name} COMMAND terraingen_app "${config}" --output-dir
                                                      "${output_dir}/config" --log-level warning)
        set_tests_properties(examples.config.${name} PROPERTIES LABELS "integration;examples")
    endforeach()
endif()

file(GLOB cpp_examples CONFIGURE_DEPENDS "${examples_dir}/cpp/example_*.cpp")
foreach(source IN LISTS cpp_examples)
    get_filename_component(example "${source}" NAME_WE)
    if(NOT example STREQUAL "example_config_load")
        add_test(NAME examples.cpp.${example} COMMAND ${example} "${output_dir}/cpp")
        set_tests_properties(examples.cpp.${example} PROPERTIES LABELS "integration;examples")
    endif()
endforeach()
add_test(NAME examples.cpp.example_config_load
         COMMAND example_config_load "${examples_dir}/config/example_craters_simple.json"
                 "${output_dir}/cpp")
set_tests_properties(examples.cpp.example_config_load PROPERTIES LABELS "integration;examples")

if(TARGET _terraingen)
    execute_process(
        COMMAND "${Python_EXECUTABLE}" -c "import numpy"
        RESULT_VARIABLE numpy_missing
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT numpy_missing)
        file(GLOB scripts CONFIGURE_DEPENDS "${examples_dir}/python/example_*.py")
        foreach(script IN LISTS scripts)
            get_filename_component(name "${script}" NAME_WE)
            add_test(NAME examples.python.${name} COMMAND "${Python_EXECUTABLE}" "${script}"
                                                          "${output_dir}/python")
            set_tests_properties(
                examples.python.${name}
                PROPERTIES ENVIRONMENT "PYTHONPATH=${PROJECT_BINARY_DIR}/python" LABELS
                           "integration;examples;python")
        endforeach()
    endif()
endif()
