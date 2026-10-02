> **This project is in active beta development.** This means the API is unstable, features may be added or removed, and breaking changes are likely to occur frequently and without notice as the design is refined.

# TerrainGen

![]()

A high-performance scientific terrain generation tool for the lunar surface.

TerrainGen provides a core C++ and Python library designed to easily integrate into external workflows. The core is designed with minimal dependencies, but provides natural extensions to handle geotiffs, meshes, and GPU acceleration.

We provide a command-line interface, a lightweight viewer, and a standalone demo that integrates TerrainGen into a planetary-scale renderer, synthesizing terrain detail on the GPU, on the fly, while navigating around the Moon.

TerrainGen is validated against existing lunar datasets. For more information, see the [validation](./validation/README.md) section.

For information on the concepts behind TerrainGen and the API, see our latest [documentation](https://alexmaietta.github.io/terraingen/).

TerrainGen plugs into downstream simulation and rendering tools such as [Project Chrono](https://github.com/projectchrono/chrono). See [Third-party Integrations and Use Cases](#third-party-integrations-and-use-cases) for current and planned integrations.

Future work includes extending TerrainGen for other planetary and celestial bodies such as Mars and asteroids.

## Getting Started

To quickly get started with TerrainGen, it is recommended to install the Python package and run the viewer.

```sh
pip install terraingen[viewer]
terraingen-viewer
```

## Installation

### Prebuilt binaries

Versioned releases are available as precompiled binaries from the GitHub releases page, built for Linux (x86-64 and AArch64), Windows (x86-64 only), and macOS (universal).

### Building from source

Install the GDAL and PROJ development packages through your platform's package manager, then configure and build with CMake.

**Linux (Debian/Ubuntu):**

```sh
sudo apt install libgdal-dev libproj-dev
```

**macOS (Homebrew):**

```sh
brew install gdal proj
```

**Windows (vcpkg):**

```sh
vcpkg install gdal proj
```

Then, from the repo root (on Windows, add `-DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake`):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/bin/terraingen --version
```



## Contributing



## Citation

If you use TerrainGen for published research, please cite:



## License



## Third-party Integrations and Use Cases

TerrainGen is designed to be integrated into downstream simulation, rendering, and machine learning pipelines. Terrain products from TerrainGen can drive physics simulations or be used to render photorealistic imagery for crater and boulder identification pipelines.

**Simulation integrations**

* [Newton](https://github.com/newton-physics/newton)
* [Nvidia Omniverse](https://www.nvidia.com/en-us/omniverse/)
* [Project Chrono](https://github.com/projectchrono/chrono)
* [IsaacLab](https://isaac-sim.github.io/IsaacLab/)

**Use cases**

* Training crater and boulder identification ML models
* Lunar surface simulations
* Lunar landing optical navigation

## Acknowledgements

TerrainGen is built on top of the following open source libraries and tools:

| Library | Purpose | License |
| --- | --- | --- |
| [GDAL](https://gdal.org/) | Raster/vector geospatial data I/O (GeoTIFF, PDS/IMG-LBL, ...) | MIT |
| [PROJ](https://proj.org/) | Coordinate reference system transformations | MIT |
| [dlpack](https://github.com/dmlc/dlpack) | Zero-copy tensor interop for raster buffers | Apache-2.0 |
| [nlohmann/json](https://github.com/nlohmann/json) | Config-based generation | MIT |
| [nanobind](https://github.com/wjakob/nanobind) | Python bindings | BSD-3-Clause |

The viewer additionally uses:

| Library | Purpose | License |
| --- | --- | --- |
| [wgpu-native](https://github.com/gfx-rs/wgpu-native) | Cross-platform WebGPU rendering | MIT / Apache-2.0 |
| [GLFW](https://www.glfw.org/) | Windowing and input | Zlib |
| [Dear ImGui](https://github.com/ocornut/imgui) | User interface | MIT |
| [GLM](https://github.com/g-truc/glm) | Vector and matrix math | MIT |
| [stb](https://github.com/nothings/stb) | PNG and JPEG texture loading (vendored in `third_party/`) | MIT or public domain |
| [tinyexr](https://github.com/syoyo/tinyexr) | OpenEXR texture loading (vendored in `third_party/`) | BSD-3-Clause |

TerrainGen also draws inspiration from existing lunar terrain generation tools:

| Name | Organization | Availability | Capabilities |
| --- | --- | --- | --- |
| [synthterrain](https://github.com/NeoGeographyToolkit/synthterrain) | NASA Ames | Open source (Apache-2.0) | Crater and boulder populations from size-frequency distributions, crater profiles and DEMs on flat terrain |
| [lunasynth](https://github.com/nasa-jpl/lunasynth) | NASA JPL | Open source (Apache-2.0) | Adds craters and rocks to a lunar base DEM, down to 1 cm pixels, and renders images under varying camera and sun positions |
| [DLES Terrain Crafting](https://doi.org/10.1109/AERO63441.2025.11068736) | NASA JSC | Not publicly released (paper) | Enhances LOLA and NAC-derived DEMs of the lunar south pole to 20 cm with synthetic craters and rocks, including hand-placed craters from imagery |
| [PANGU](https://pangu.software/) | University of Dundee / STAR-Dundee | Commercial | Multi-resolution planet and asteroid surfaces from real or fractal DEMs with statistical craters and boulders; GPU camera, LiDAR and thermal sensor simulation |

For a complete list of references, see the [TerrainGen paper](./paper/paper.md).
