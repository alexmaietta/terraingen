terraingen_add_test_suite(app SOURCES cli_test.cpp LIBRARIES terraingen_cli nlohmann_json)
if(TERRAINGEN_BUILD_VIEWER)
    add_subdirectory(viewer)
endif()
