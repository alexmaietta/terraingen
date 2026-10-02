# stb_image (PNG, JPEG) and stb_image_write (PNG, JPEG, plus the zlib compressor tinyexr uses),
# vendored in third_party/stb and compiled once.
add_library(ernie_stb STATIC "${ERNIE_SOURCE_DIR}/third_party/stb/stb.cpp")
target_include_directories(ernie_stb SYSTEM PUBLIC "${ERNIE_SOURCE_DIR}/third_party/stb")
target_compile_definitions(ernie_stb PUBLIC STBI_ONLY_PNG STBI_ONLY_JPEG)
set_target_properties(ernie_stb PROPERTIES POSITION_INDEPENDENT_CODE ON)
