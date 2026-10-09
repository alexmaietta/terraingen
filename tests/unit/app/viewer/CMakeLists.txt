terraingen_add_test_suite(viewer_scene SOURCES terrain_scene_test.cpp LIBRARIES terraingen_viewer)
terraingen_add_test_suite(
    viewer
    SOURCES
    viewer_app_test.cpp
    LIBRARIES
    terraingen_viewer
    LABELS
    gpu)
