include("${ERNIE_SOURCE_DIR}/cmake/ErnieTesting.cmake")

ernie_add_test_suite(
    unit
    SOURCES
    camera/orbit_controller_test.cpp
    camera/camera_test.cpp
    camera/sensor_filters_test.cpp
    core/image_test.cpp
    core/spectrum_test.cpp
    core/umbrella_test.cpp
    geometry/frustum_test.cpp
    geometry/lod_quadtree_test.cpp
    geometry/mesh_data_test.cpp
    geometry/primitives_test.cpp
    gpu/shader_compiler_test.cpp
    interop/tensor_test.cpp
    material/brdf_test.cpp
    raster/poisson_disk_test.cpp
    raster/raster_delegate_test.cpp
    render/lights_test.cpp
    render/render_delegate_test.cpp
    render/resolve_test.cpp
    render/sampling_test.cpp
    raster/shadow_cascades_test.cpp
    scene/path_test.cpp
    scene/scene_test.cpp
    scene/schema_test.cpp
    LIBRARIES
    ernie
    ernie_stb
    ernie_tinyexr
    DEFINITIONS
    ERNIE_TEST_FIXTURES="${ERNIE_SOURCE_DIR}/tests/fixtures")
ernie_add_test_suite(
    gpu
    SOURCES
    gpu/pipeline_test.cpp
    interop/cuda_interop_test.cpp
    gpu/vulkan_context_test.cpp
    material/brdf_vulkan_test.cpp
    raster/hex_tiling_gpu_test.cpp
    raster/mesh_part_test.cpp
    raster/raster_delegate_gpu_test.cpp
    raster/terrain_part_test.cpp
    LIBRARIES
    ernie
    LABELS
    gpu
    DEFINITIONS
    ERNIE_SHADER_DIR="${ERNIE_SOURCE_DIR}/ernie/src/shaders")

if(TARGET _ernie)
    execute_process(
        COMMAND "${Python_EXECUTABLE}" -c "import pytest, numpy"
        RESULT_VARIABLE ernie_python_test_deps_missing
        OUTPUT_QUIET ERROR_QUIET)
    if(ernie_python_test_deps_missing)
        message(WARNING "pytest or numpy not found for ${Python_EXECUTABLE}; skipping Ernie's "
                        "Python tests")
    else()
        add_test(NAME ernie.python.pytest
                 COMMAND "${Python_EXECUTABLE}" -m pytest -q -p no:cacheprovider
                         "${CMAKE_CURRENT_SOURCE_DIR}/python")
        set_tests_properties(
            ernie.python.pytest PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/python"
                                           LABELS "gpu;python")
    endif()
endif()

if(TARGET ernie_gui)
    ernie_add_test_suite(
        gui
        SOURCES
        gui/panel_test.cpp
        gui/session_test.cpp
        gui/theme_test.cpp
        LIBRARIES
        ernie_gui_internal)
    ernie_add_test_suite(
        gui_gpu
        SOURCES
        gui/viewer_gpu_test.cpp
        LIBRARIES
        ernie_gui_internal
        LABELS
        gpu)
endif()
