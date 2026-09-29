terraingen_add_test_suite(render SOURCES gpu_context_names_test.cpp LIBRARIES terraingen_render)
terraingen_add_test_suite(
    render_gpu
    SOURCES
    gpu_context_test.cpp
    glfw_surface_test.cpp
    LIBRARIES
    terraingen_render
    LABELS
    gpu)
