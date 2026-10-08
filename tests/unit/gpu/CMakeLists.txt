terraingen_add_test_suite(
    gpu
    SOURCES
    backend_test.cpp
    vulkan_test.cpp
    LIBRARIES
    terraingen::gpu
    terraingen::noise)
if(TERRAINGEN_WITH_VULKAN)
    terraingen_add_slang_kernel(terraingen_tests_gpu terraingen_test_hash_conformance
                                kernels/hash_conformance.slang)
    target_compile_definitions(terraingen_tests_gpu PRIVATE TERRAINGEN_TEST_KERNELS)
endif()
