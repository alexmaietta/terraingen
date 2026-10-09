terraingen_add_test_suite(gui_scene SOURCES config_editor_test.cpp terrain_scene_test.cpp LIBRARIES
                          terraingen_gui)
terraingen_add_test_suite(
    gui
    SOURCES
    gui_app_test.cpp
    LIBRARIES
    terraingen_gui
    LABELS
    gpu)
