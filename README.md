> ⚠️ **This project is in active beta development.** This means the API is unstable, features may be added or removed, and breaking changes are likely to occur frequently and without notice as the design is refined.

# TerrainGen

![]()

A scientific terrain generation tool for the lunar surface.

TerrainGen provides a core C++ and Python library designed to easily integrate into external workflows. We also provide an optional lightweight viewer to explore TerrainGen's capabilities.

Orbital datasets are not high enough resolution (yet) to accurately model centimeter-scale terrain and hazards. Whether its a planetary rover mission or a landing site safety analysis, generating realistic, stochastic, and high resolution terrains is mission critical.

While there is active research in the aerospace community on how to best represent high-resolution lunar terrain, there is very limited open-source software...

TerrainGen is designed to be modular and easy to use, both in a standalone fashion or integrated into existing projects. We provide examples of [third-party integrations and use cases](#third-party-integrations-and-use-cases) below. Users can build their own distributions, crater profiles, boulder models, and much more, including customizing or building their own pipeline to automate the process.

Below are existing tools that are open-source, closed-source, or commercial.

| Name | Type | Organization | Notes |
| --- | --- | --- | --- |
| [synthterrain](https://github.com/NeoGeographyToolkit/synthterrain) | Software | NASA Ames | Limited to cratering flat terrain, but has great crater and boulder distribution functions |
| [Viper Simulation](https://ntrs.nasa.gov/api/citations/20250004026/downloads/DigitalTwins3-2025-04-23-RSIMLunarSurfaceSim.pdf) | Paper | NASA Ames | |
| [lunasynth](https://github.com/nasa-jpl/lunasynth) | Software | NASA JPL | Not regularly maintained or updated |
| [DLES Terrain Crafting](https://doi.org/10.1109/AERO63441.2025.11068736) | Paper | NASA JSC | |
| [PANGU](https://pangu.software/) | Commercial software | University of Dundee / STAR-Dundee | Gold standard for synthetic terrain modeling; behind a paywall and licensing restrictions |

## Getting Started

To quickly get started with TerrainGen, it is recommended to install the Python package and run the viewer.

```sh
pip install terraingen[viewer]
terraingen-viewer
```

## Installation

### Prebuilt binaries

Versioned releases are available as precompiled binaries from the GitHub releases page, built for Linux (x86-64 and AArch64), Windows (x86-64 only), and macOS (universal). This is the recommended way to use the software.

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

## Open Source Software

TerrainGen is built on top of the following open source libraries and tools:

| Library | Purpose | License | Status |
| --- | --- | --- | --- |
| [GDAL](https://gdal.org/) | Raster/vector geospatial data I/O (GeoTIFF, PDS/IMG-LBL, ...) | MIT | Planned |
| [PROJ](https://proj.org/) | Coordinate reference system transformations | MIT | Planned |
| [dlpack](https://github.com/dmlc/dlpack) | Zero-copy tensor interop for raster buffers | Apache-2.0 | Planned (vendored) |
| [nlohmann/json](https://github.com/nlohmann/json) | Config-based generation | MIT | Planned (vendored) |
| [spdlog](https://github.com/gabime/spdlog) | Logging backend | MIT | Planned (fetched via CMake) |
| [nanobind](https://github.com/wjakob/nanobind) | Python bindings | BSD-3-Clause | Planned |

## Contributing



## Citation

If you use TerrainGen for published research, please cite:



## License



## Third-party Integrations and Use Cases

TerrainGen is designed to be integrated into downstream simulation, rendering, and machine learning pipelines. Terrain products from TerrainGen can drive physics simulations or be used to render photorealistic imagery for crater and boulder identification pipelines.

**Simulation integrations**

* [Newton](https://github.com/newton-physics/newton)
* [Nvidia Omniverse](https://www.nvidia.com/en-us/omniverse/)
* [Project Chrono](https://projectchrono.org/)
* [IsaacLab](https://isaac-sim.github.io/IsaacLab/)

**Use cases**

* Training crater and boulder identification ML models
* Lunar surface simulations
* Lunar landing/approach simulations

Related, in-progress projects: LRO Toolkit and Crater & Boulder ID.

Want to get started with actual lunar terrain datasets? See Overview of Lunar Orbiters and Datasets (in work).

## Verification and Validation of TerrainGen

See our validation scripts and results [here](./vv/README.md)
