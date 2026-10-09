terraingen_add_test_suite(app SOURCES cli_test.cpp LIBRARIES terraingen_cli nlohmann_json)
if(TERRAINGEN_BUILD_GUI)
    add_subdirectory(gui)
endif()
