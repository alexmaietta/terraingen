# DLPack (tensor exchange header), vendored in third_party/dlpack.
add_library(ernie_dlpack INTERFACE)
target_include_directories(ernie_dlpack SYSTEM INTERFACE "${ERNIE_SOURCE_DIR}/third_party/dlpack/include")
