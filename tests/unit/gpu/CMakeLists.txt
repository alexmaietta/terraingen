terraingen_add_test_suite(
    gpu
    SOURCES
    backend_test.cpp
    noise_test.cpp
    vulkan_test.cpp
    LIBRARIES
    terraingen::gpu
    terraingen::noise)
terraingen_add_slang_kernel(terraingen_tests_gpu terraingen_test_hash_conformance
                            kernels/hash_conformance.slang)
terraingen_add_slang_kernel(terraingen_tests_gpu terraingen_test_noise_conformance
                            kernels/noise_conformance.slang)
