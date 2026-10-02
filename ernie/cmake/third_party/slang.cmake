set(ERNIE_DEP_VERSION_slang
    2026.19
    CACHE STRING "Release of Slang to fetch. Changing it requires updating the hashes below.")
mark_as_advanced(ERNIE_DEP_VERSION_slang)

# Prebuilt Slang: the shader compiler library Ernie links to compile Slang modules to SPIR-V at
# run time. The Linux builds target old glibc versions so they also run on Ubuntu 22.04.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(slang_platform linux-x86_64-glibc-2.27)
    set(slang_sha256 ae348a33d906ed29d14c2294f2260746cb2c3eb4aa2c7fc4bfd887a3a298737d)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(slang_platform linux-aarch64-glibc-2.28)
    set(slang_sha256 52b1e6b2aad2d9a119308fe8f99d1699d1854e4ac2d7b579cf0f8f25349bcd17)
elseif(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(slang_platform macos-aarch64)
    set(slang_sha256 f77d89b91ec63698a7a8e5aa4316ce08b70d3211a236640ee4551f55be85d9ed)
elseif(APPLE)
    set(slang_platform macos-x86_64)
    set(slang_sha256 c5e18233c0b90cc651c792ec4c4e4a467a9a101ffb6301643aab89c016b48e42)
elseif(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
    set(slang_platform windows-x86_64)
    set(slang_sha256 fc922f214b5d115f663932e59e80e0f6b6cc6fae140979cf307da6459b6f8f33)
else()
    message(FATAL_ERROR "No prebuilt Slang for ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}")
endif()

include(FetchContent)

FetchContent_Declare(
    slang
    URL "https://github.com/shader-slang/slang/releases/download/v${ERNIE_DEP_VERSION_slang}/slang-${ERNIE_DEP_VERSION_slang}-${slang_platform}.zip"
    URL_HASH SHA256=${slang_sha256}
    SOURCE_SUBDIR no-build)
FetchContent_MakeAvailable(slang)

find_package(
    slang
    CONFIG
    REQUIRED
    PATHS
    "${slang_SOURCE_DIR}/lib/cmake/slang"
    NO_DEFAULT_PATH)

# Visible to applications that install Slang's runtime library beside their binaries.
set_target_properties(slang::slang PROPERTIES IMPORTED_GLOBAL TRUE)
