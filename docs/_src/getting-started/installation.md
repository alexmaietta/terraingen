# Installation

We recommend installing `terraingenpy` from PyPI, which gives you the Python package and the `terraingen` command. Build from source to use the library from your own C++ project, or for the GUI until the package includes it.

## Requirements

- Linux (Ubuntu 22.04 or newer), macOS 14 or newer, or Windows 10 or newer, on x86_64 or arm64.
- Python 3.10 or newer for the Python package.
- Optional: a Vulkan, CUDA or Metal driver for GPU acceleration. Run `terraingen backends` to see what is available.

## Python package

```sh
pip install terraingenpy
```

The package imports as `terraingen` and installs the `terraingen` command. There is a wheel for Python 3.10, one for 3.11, and one for 3.12 and newer. The wheels do not include the GUI yet; it will come with the package later. They read and write GeoTIFFs on their own, without GDAL.

## Releases

Every [release](https://github.com/alexmaietta/terraingen/releases) has the CLI, library, headers and CMake package for each platform, and `.deb` packages for Ubuntu and Debian. The [dev](https://github.com/alexmaietta/terraingen/releases/tag/dev) pre-release is built from `main` on every push.

## Building from source

You need CMake 3.22 or newer and a C++17 compiler (tested with GCC 11, Clang 14, Apple Clang 15 and MSVC 2022). CMake fetches everything else.

```sh
git clone https://github.com/alexmaietta/terraingen
cd terraingen
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
cmake --install build --prefix /usr/local
```

To build the Python package from source, run `pip install .` instead.

These options choose what is built:

| Option | Default | Builds |
| --- | --- | --- |
| `TERRAINGEN_BUILD_PYTHON` | `ON` | The Python bindings, into `build/python` |
| `TERRAINGEN_BUILD_APP` | `ON` | The `terraingen` command |
| `TERRAINGEN_BUILD_GUI` | `ON` | `terraingen --gui`, when `TERRAINGEN_ERNIE_DIR` points at an Ernie checkout |
| `TERRAINGEN_BUILD_TESTS` | `ON` at the top level | The test suite |
| `TERRAINGEN_INSTALL` | `ON` at the top level | The install rules and CMake package |

The GUI builds with Ernie, the renderer, from a local checkout: `-DTERRAINGEN_ERNIE_DIR=../ernie`. Without one, configuring warns and builds everything else; `-DTERRAINGEN_BUILD_GUI=OFF` leaves the GUI out without the warning.

## Docker

```sh
docker run --rm ghcr.io/alexmaietta/terraingen:latest terraingen --help
```

The `latest` tag is the newest release, and each release is also tagged with its version without the `v`, such as `0.2.0` and `0.2`. The `dev` tag follows `main`.

## Using TerrainGen from C++

Once installed, find the CMake package and link the library:

```cmake
find_package(terraingen REQUIRED)
target_link_libraries(app PRIVATE terraingen::terraingen)
```

```cpp
#include <terraingen/terraingen.h>
```

## Checking the installation

```sh
terraingen --version
terraingen backends
```

`terraingen backends` lists the GPU backends this machine can run; without one, everything runs on the CPU.
