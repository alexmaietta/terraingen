set(ERNIE_DEP_VERSION_wgpu_native
    v29.0.1.1
    CACHE STRING "Release of wgpu-native to fetch. Changing it requires updating the hashes below.")
mark_as_advanced(ERNIE_DEP_VERSION_wgpu_native)

include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

if(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(wgpu_platform windows-aarch64-msvc)
        set(wgpu_sha256 4a876421a8c1e5fe72f849b3722214280fe485cb1c56f77f8b0c82414be5b29f)
    elseif(MSVC)
        set(wgpu_platform windows-x86_64-msvc)
        set(wgpu_sha256 7e67d7445c42aeb85e30f88930fd8d7d83ee769e3390aeb1ada75ebf3cf78132)
    else()
        set(wgpu_platform windows-x86_64-gnu)
        set(wgpu_sha256 d471e3614733c1d4ddd61bfd19868356477d0d37bf531bf8c6cb64a7f579bd2a)
    endif()
elseif(APPLE)
    if(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64" OR (NOT CMAKE_OSX_ARCHITECTURES
                                                    AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64"))
        set(wgpu_platform macos-x86_64)
        set(wgpu_sha256 8e2f7378548ddd0e2cf21e7d864dda46e953f0af724855a33778b85ead206d41)
    else()
        set(wgpu_platform macos-aarch64)
        set(wgpu_sha256 a5797a37b1adf720bcd5dcffb291edbbd5b7b14be0a3874c28e6393a655a7a3e)
    endif()
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
        set(wgpu_platform linux-aarch64)
        set(wgpu_sha256 015fcdf1dbae82e614a783cc38017e5399ae0927a889fe9b69c9b664bc61b47a)
    else()
        set(wgpu_platform linux-x86_64)
        set(wgpu_sha256 95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a)
    endif()
endif()

FetchContent_Declare(
    wgpu_native
    URL "https://github.com/gfx-rs/wgpu-native/releases/download/${ERNIE_DEP_VERSION_wgpu_native}/wgpu-${wgpu_platform}-release.zip"
    URL_HASH SHA256=${wgpu_sha256})
FetchContent_MakeAvailable(wgpu_native)

if(WIN32)
    set(wgpu_library "${wgpu_native_SOURCE_DIR}/lib/wgpu_native.lib")
    if(NOT EXISTS "${wgpu_library}")
        set(wgpu_library "${wgpu_native_SOURCE_DIR}/lib/libwgpu_native.a")
    endif()
    set(wgpu_system_libraries
        d3dcompiler
        ws2_32
        userenv
        bcrypt
        ntdll
        opengl32
        Propsys
        RuntimeObject)
elseif(APPLE)
    set(wgpu_library "${wgpu_native_SOURCE_DIR}/lib/libwgpu_native.a")
    set(wgpu_system_libraries "-framework CoreFoundation" "-framework QuartzCore"
                              "-framework Metal" "-framework Cocoa")
else()
    set(wgpu_library "${wgpu_native_SOURCE_DIR}/lib/libwgpu_native.a")
    find_package(Threads REQUIRED)
    set(wgpu_system_libraries Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

add_library(wgpu_native STATIC IMPORTED GLOBAL)
set_target_properties(
    wgpu_native
    PROPERTIES IMPORTED_LOCATION "${wgpu_library}"
               INTERFACE_INCLUDE_DIRECTORIES "${wgpu_native_SOURCE_DIR}/include"
               INTERFACE_LINK_LIBRARIES "${wgpu_system_libraries}")
