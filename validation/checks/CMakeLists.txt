terraingen_add_test_suite(
    validation
    SOURCES
    size_frequency_check.cpp
    crater_profile_check.cpp
    LIBRARIES
    terraingen::body
    LABELS
    validation)
target_compile_definitions(
    terraingen_tests_validation
    PRIVATE TERRAINGEN_VALIDATION_REFERENCES="${PROJECT_SOURCE_DIR}/validation/references")

add_executable(terraingen_geotiff_roundtrip geotiff_roundtrip.cpp)
target_link_libraries(terraingen_geotiff_roundtrip PRIVATE terraingen::raster terraingen_warnings)

set(TERRAINGEN_VALIDATION_PYTHON
    "${PROJECT_SOURCE_DIR}/.venv/bin/python"
    CACHE FILEPATH "Python with validation/requirements.txt installed, for GDAL cross-checks")
execute_process(
    COMMAND "${TERRAINGEN_VALIDATION_PYTHON}" -c "import rasterio, numpy"
    RESULT_VARIABLE validation_python_missing
    OUTPUT_QUIET ERROR_QUIET)
if(validation_python_missing)
    message(STATUS "rasterio not available to ${TERRAINGEN_VALIDATION_PYTHON}; "
                   "skipping GDAL cross-checks")
else()
    add_test(
        NAME validation.geotiff_gdal
        COMMAND
            "${TERRAINGEN_VALIDATION_PYTHON}"
            "${PROJECT_SOURCE_DIR}/validation/scripts/check_geotiff_with_gdal.py"
            $<TARGET_FILE:terraingen_geotiff_roundtrip> "${CMAKE_CURRENT_BINARY_DIR}/geotiff")
    set_tests_properties(validation.geotiff_gdal PROPERTIES LABELS validation)
endif()
