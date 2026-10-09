# The viewer behind `terraingen view`: TerrainGen's panels and scene on Ernie's app.
add_library(terraingen_viewer STATIC config_editor.cpp terrain_scene.cpp viewer_app.cpp)
target_include_directories(terraingen_viewer PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>")
target_link_libraries(
    terraingen_viewer
    PUBLIC terraingen::terraingen ernie_app nlohmann_json
    PRIVATE terraingen_warnings terraingen_coverage)
