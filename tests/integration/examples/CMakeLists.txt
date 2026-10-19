# Runs every example: each config through the terraingen command, each C++ example and each
# Python example (when the bindings and NumPy are available) with its checks (`--test`), and
# `terraingen-examples` listing them and, with the GUI, opening one hidden. Outputs go under the
# build tree.
set(examples_dir "${PROJECT_SOURCE_DIR}/examples")
set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/output")

if(TARGET terraingen_app)
    file(GLOB_RECURSE configs CONFIGURE_DEPENDS "${examples_dir}/config/example_*.json")
    foreach(config IN LISTS configs)
        get_filename_component(name "${config}" NAME_WE)
        add_test(NAME examples.config.${name} COMMAND terraingen_app "${config}" --output-dir
                                                      "${output_dir}/config" --log-level warning)
        set_tests_properties(examples.config.${name} PROPERTIES LABELS "integration;examples")
    endforeach()
endif()

file(GLOB_RECURSE cpp_examples CONFIGURE_DEPENDS "${examples_dir}/cpp/example_*.cpp")
foreach(source IN LISTS cpp_examples)
    get_filename_component(example "${source}" NAME_WE)
    add_test(NAME examples.cpp.${example} COMMAND ${example} --test --output-dir
                                                  "${output_dir}/cpp")
    set_tests_properties(examples.cpp.${example} PROPERTIES LABELS "integration;examples")
endforeach()

add_test(NAME examples.browser.list COMMAND terraingen_examples --list)
set_tests_properties(examples.browser.list PROPERTIES LABELS "integration;examples"
                                                      PASS_REGULAR_EXPRESSION "craters_simple")
if(TERRAINGEN_GUI)
    add_test(NAME examples.browser.window COMMAND terraingen_examples craters_simple --headless
                                                  --test)
    set_tests_properties(examples.browser.window PROPERTIES LABELS "integration;examples;gpu")
endif()

if(TARGET _terraingen)
    execute_process(
        COMMAND "${Python_EXECUTABLE}" -c "import numpy"
        RESULT_VARIABLE numpy_missing
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT numpy_missing)
        file(GLOB_RECURSE scripts CONFIGURE_DEPENDS "${examples_dir}/python/example_*.py")
        foreach(script IN LISTS scripts)
            get_filename_component(name "${script}" NAME_WE)
            add_test(NAME examples.python.${name}
                     COMMAND "${Python_EXECUTABLE}" "${script}" --viewer null --test --output-dir
                             "${output_dir}/python")
            set_tests_properties(
                examples.python.${name}
                PROPERTIES ENVIRONMENT "PYTHONPATH=${PROJECT_BINARY_DIR}/python" LABELS
                           "integration;examples;python")
        endforeach()
        add_test(NAME examples.python.list COMMAND "${Python_EXECUTABLE}" "${examples_dir}/python")
        set_tests_properties(
            examples.python.list
            PROPERTIES ENVIRONMENT "PYTHONPATH=${PROJECT_BINARY_DIR}/python" LABELS
                       "integration;examples;python" PASS_REGULAR_EXPRESSION "craters_simple")
    endif()
endif()

# The examples run longest, so a parallel ctest without timings from an earlier run starts them
# before the unit tests instead of leaving a minutes-long example for the end.
get_property(
    example_tests
    DIRECTORY
    PROPERTY TESTS)
set_tests_properties(${example_tests} PROPERTIES COST 10)
