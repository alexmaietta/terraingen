# stb_image (PNG, JPEG) and stb_image_write (PNG, JPEG, plus the zlib compressor tinyexr uses),
# vendored in third_party/stb and compiled once.
add_library(terraingen_stb STATIC "${PROJECT_SOURCE_DIR}/third_party/stb/stb.cpp")
target_include_directories(terraingen_stb SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/third_party/stb")
target_compile_definitions(terraingen_stb PUBLIC STBI_ONLY_PNG STBI_ONLY_JPEG)
set_target_properties(terraingen_stb PROPERTIES POSITION_INDEPENDENT_CODE ON)
