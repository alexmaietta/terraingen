# Installation

TerrainGen is a C++20 library with Python bindings. It builds on Linux, macOS and Windows; Ubuntu
22.04 (GCC 11, CMake 3.22, Python 3.10) is the oldest supported Linux.

## Python package

```sh
pip install terraingen
```

To build the package from a checkout instead:

```sh
pip install .
```

## Building from source

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
cmake --install build --prefix /opt/terraingen
```

The build needs a C++20 compiler and CMake 3.22 or newer. The Python bindings need Python 3.10 or
newer with its development headers; nanobind is used from the Python environment when installed,
and fetched otherwise. GoogleTest is fetched for the tests. JSON and Deflate support are vendored
in `third_party/`, so the library has no other dependencies.

| Option | Default | Effect |
| --- | --- | --- |
| `TERRAINGEN_BUILD_PYTHON` | `ON` | Build the Python bindings |
| `TERRAINGEN_BUILD_TESTS` | `ON` at top level | Build the test suite |
| `TERRAINGEN_BUILD_VALIDATION` | `ON` at top level | Build the checks against external references |
| `TERRAINGEN_INSTALL` | `ON` at top level | Generate install rules and the CMake package |
| `TERRAINGEN_COVERAGE` | `OFF` | Instrument for coverage and add a `coverage` target |

## Using TerrainGen from CMake

After installing, find the package and link the modules you need, or `terraingen::terraingen` for
all of them:

```cmake
find_package(terraingen REQUIRED)
target_link_libraries(my_app PRIVATE terraingen::terraingen)
```

Each module is its own library (see {doc}`concepts/overview`), so a program that only reads
GeoTIFFs can link `terraingen::io` alone. `GetBuildInfo()` comes from `terraingen::build_info`,
which `terraingen::terraingen` includes.

On Debian and Ubuntu, `cpack -G DEB` in the build directory makes a `terraingen-dev` package with
the headers, libraries and CMake package.

## Docker

The `Dockerfile` at the repository root builds and tests TerrainGen and its Python bindings on
Ubuntu 22.04, then provides an image with the installed library, CMake package and Python package
for building against TerrainGen:

```sh
docker build -t terraingen .
docker run --rm -it terraingen python3 -c "import terraingen; print(terraingen.version())"
```
