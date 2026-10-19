# The GUI behind `terraingen --gui`: TerrainGen's panels and scene on Ernie's app.
add_library(terraingen_gui STATIC config_editor.cpp gui_app.cpp terrain_scene.cpp)
target_include_directories(terraingen_gui PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>")
target_link_libraries(
    terraingen_gui
    PUBLIC terraingen::terraingen ernie_app nlohmann_json
    PRIVATE terraingen_warnings terraingen_coverage)
