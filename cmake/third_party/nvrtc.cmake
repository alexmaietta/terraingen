# SPDX-License-Identifier: BSD-3-Clause

set(TERRAINGEN_DEP_VERSION_nvrtc
    12.4.127
    CACHE STRING "Release of NVRTC to fetch. Changing it requires updating the hashes below.")
mark_as_advanced(TERRAINGEN_DEP_VERSION_nvrtc)

# NVIDIA's redistributable NVRTC, for slangc only: it compiles kernels to PTX for the CUDA backend
# at build time, and nothing of it ships or runs with TerrainGen, which needs only the NVIDIA driver.
# It comes from an early CUDA 12 release because PTX loads only on drivers at least as new as the
# NVRTC that made it. There is no NVRTC (or CUDA) on Apple platforms, whose builds have no PTX.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(nvrtc_platform linux-x86_64)
    set(nvrtc_archive tar.xz)
    set(nvrtc_sha256 b5e7a984fcf05d3123684d7926e595306d31fbf99f9b19e9a0d268a02fc75827)
elseif(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(nvrtc_platform linux-sbsa)
    set(nvrtc_archive tar.xz)
    set(nvrtc_sha256 f9e4bd972ffee5951577b45524b656eda681407a3c761c57978acec26a3acc25)
elseif(CMAKE_HOST_WIN32 AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "AMD64|x86_64")
    set(nvrtc_platform windows-x86_64)
    set(nvrtc_archive zip)
    set(nvrtc_sha256 f140545e06d0d10780c1382a577db2e2c242db7a2d94970f0e6026b2d01aeb1b)
endif()

set(TERRAINGEN_NVRTC_DIR "")
set(TERRAINGEN_NVRTC_ROOT "")
if(nvrtc_platform)
    include(FetchContent)
    FetchContent_Declare(
        terraingen_nvrtc
        URL "https://developer.download.nvidia.com/compute/cuda/redist/cuda_nvrtc/${nvrtc_platform}/cuda_nvrtc-${nvrtc_platform}-${TERRAINGEN_DEP_VERSION_nvrtc}-archive.${nvrtc_archive}"
        URL_HASH SHA256=${nvrtc_sha256}
        SOURCE_SUBDIR no-build)
    FetchContent_MakeAvailable(terraingen_nvrtc)
    set(TERRAINGEN_NVRTC_ROOT "${terraingen_nvrtc_SOURCE_DIR}")
    # The folder slangc loads NVRTC from: its DLLs on Windows, its shared libraries elsewhere.
    if(CMAKE_HOST_WIN32)
        set(TERRAINGEN_NVRTC_DIR "${terraingen_nvrtc_SOURCE_DIR}/bin")
    else()
        set(TERRAINGEN_NVRTC_DIR "${terraingen_nvrtc_SOURCE_DIR}/lib")
    endif()
endif()
