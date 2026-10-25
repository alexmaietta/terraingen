> **This project is in active beta development.** This means the API is unstable, features may be added or removed, and breaking changes are likely to occur frequently and without notice as the design is refined.

[![pipeline](https://img.shields.io/github/actions/workflow/status/alexmaietta/terraingen/ci.yml?branch=main&label=pipeline)](https://github.com/alexmaietta/terraingen/actions/workflows/ci.yml)
[![coverage](https://img.shields.io/endpoint?url=https://alexmaietta.github.io/terraingen/coverage/badge.json)](https://alexmaietta.github.io/terraingen/coverage/)
[![release](https://img.shields.io/github/v/release/alexmaietta/terraingen)](https://github.com/alexmaietta/terraingen/releases)
[![pypi](https://img.shields.io/pypi/v/terraingenpy)](https://pypi.org/project/terraingenpy/)
[![license](https://img.shields.io/badge/license-BSD_3--Clause-blue)](LICENSE)

# TerrainGen

A high-performance scientific terrain generation tool for interplanetary bodies.

![Blue Moon MK1 SN101 lander at Connecting Ridge, with Mons Malapert in the background, split between the source elevation model and TerrainGen's upscaled terrain](https://raw.githubusercontent.com/alexmaietta/terraingen/main/docs/_src/images/hero_mk1_sn101_connecting_ridge_split.jpg)

*Blue Moon MK1 SN101 lander at Connecting Ridge near the lunar south pole, with Mons Malapert in the background. Left: the 5 m/px LOLA elevation models as they are. Right: the same terrain upscaled by TerrainGen.*

TerrainGen can:

- Generate synthetic terrain from scratch, or add fine detail to existing elevation models.
- Populate craters and boulders from cumulative size-frequency distributions.
- Model each crater's profile, age, degradation, irregularity.
- Bake small boulders into the heightmap, and generate procedural 3D boulder models for the large ones.
- Produce synthetic albedo maps with crater ejecta and rays that fade with crater age.
- Read GeoTIFF elevation and albedo maps, and ICQ, OBJ, glTF and STL shape models.
- Write GeoTIFF heightmaps and albedo, material and photometry maps, crater and boulder lists as CSV or JSON, and terrain, boulder and body meshes as glTF, OBJ or STL.
- Model complex geologic features such as complex and secondary craters, boulder ejecta fields, dunes and wind ripples, layered strata terraced into benches and cliffs, fractured bedrock pavement, wind-carved ventifacts, and sand pooled in lows or trailing behind boulders.
- Be extended through base classes in C++ or Python with custom size-frequency distributions, crater ages, profiles and degradation, albedo, sources, and layers.
- Run noise and crater shaping on the GPU (Vulkan, CUDA or Metal), with everything else on the CPU.
- Generate deterministic results for the same seed on any grid, point query or thread count.
- Integrate into simulations and other external workflows.

We also provide:

- A C++17 library with Python bindings and minimal dependencies.
- Smart and tested defaults for various surface features.
- A command-line interface that generates terrain from JSON configuration files.
- A GUI for editing configurations and viewing the results in 3D, in builds made with Ernie, the renderer.
- Planned: a standalone demo that streams TerrainGen detail into a planetary-scale renderer on the GPU while navigating around the Moon.
- Planned: example integrations with third-party tools such as Project Chrono, NASA Trick, IsaacLab, and Newton.

For more information on the concepts behind TerrainGen and the API, see our latest [documentation](https://alexmaietta.github.io/terraingen/).

## Quick Start

We recommend getting started by installing `terraingenpy` from PyPI and generating a cratered terrain.

```sh
pip install terraingenpy
```

```python
import terraingen

craters = terraingen.CraterLayer()
terrain = terraingen.TerrainGenerator(terraingen.FlatSource(257, 257, 1.0), [craters])
heights = terraingen.evaluate(terrain)
```

The wheels leave out the GUI; `terraingen --gui` needs a build from source with Ernie (see [CONTRIBUTING.md](CONTRIBUTING.md)).

## Requirements

- Linux (Ubuntu 22.04 or newer), macOS 14 or newer, or Windows 10 or newer, on x86_64 or arm64.
- Python 3.10 or newer for the Python package.
- Optional: a Vulkan, CUDA or Metal driver for GPU acceleration. Run `terraingen backends` to see what is available.

## Installation

### Releases

Every [release](https://github.com/alexmaietta/terraingen/releases) has the CLI, library, headers and CMake package for each platform, and `.deb` packages for Ubuntu and Debian. The [dev](https://github.com/alexmaietta/terraingen/releases/tag/dev) pre-release is built from `main` on every push.

### Building from Source

You need CMake 3.22 or newer and a C++17 compiler (tested with GCC 11, Clang 14, Apple Clang 15 and MSVC 2022). CMake fetches everything else.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
cmake --install build --prefix /usr/local
```

To build the Python package from source, run `pip install .` instead.

### Docker

```sh
docker run --rm ghcr.io/alexmaietta/terraingen:latest terraingen --help
```

The `latest` tag is the newest release, and each release is also tagged with its version without the `v`, such as `0.2.0` and `0.2`. The `dev` tag follows `main`.

## Getting Started

### Python

```python
import terraingen

# A flat 513 x 513 grid with 2 m pixels
ground = terraingen.FlatSource(513, 513, 2.0)

# Fractal noise 40 m high with an 800 m wavelength
fractal = terraingen.FractalParams(seed=1, wavelength=800.0, amplitude=40.0)
noise = terraingen.NoiseLayer(fractal)
relief = terraingen.NoiseRelief([noise])

terrain = terraingen.TerrainGenerator(ground, [relief])
heights = terraingen.evaluate(terrain)
terraingen.write_geotiff("terrain.tif", heights)
```

### C++

```cmake
find_package(terraingen REQUIRED)
target_link_libraries(app PRIVATE terraingen::terraingen)
```

```cpp
#include <terraingen/terraingen.h>

int main() {
    // Fractal noise 40 m high with an 800 m wavelength
    terraingen::NoiseLayer noise;
    noise.fractal.seed = 1;
    noise.fractal.wavelength = 800.0f;
    noise.fractal.amplitude = 40.0f;

    terraingen::SyntheticTerrainParams relief;
    relief.layers = {noise};

    // A flat 513 x 513 grid with 2 m pixels, plus the noise
    terraingen::TerrainGenerator terrain;
    terrain.source = std::make_shared<terraingen::FlatSource>(513, 513, 2.0);
    terrain.layers.push_back(std::make_shared<terraingen::NoiseRelief>(relief));

    terraingen::Raster<float> heights = terraingen::Evaluate(terrain);
    terraingen::WriteGeoTiff("terrain.tif", heights);
}
```

### Demos

Planned: larger showcases, such as terrain over a whole asteroid built from the Itokawa shape model.

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for how to build, test and submit changes.

### Documentation

The [documentation](https://alexmaietta.github.io/terraingen/) is built with MkDocs and Doxygen from [docs/](docs/). Run `make -C docs build` to build it locally.

### Code Coverage

The [coverage report](https://alexmaietta.github.io/terraingen/coverage/) is updated on every push to `main`. To generate it locally:

```sh
cmake -S . -B build-coverage -DTERRAINGEN_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-coverage -j 8 --target coverage
```

## Citation

If you use TerrainGen for published research, please cite:

```bibtex
@software{maietta_terraingen,
  author  = {Maietta, Alex},
  title   = {{TerrainGen}: An open-source scientific terrain generation suite for planetary surfaces},
  url     = {https://github.com/alexmaietta/terraingen},
  version = {0.2.0},
  license = {BSD-3-Clause},
  year    = {2026}
}
```

## License

TerrainGen is licensed under the [BSD 3-Clause License](LICENSE).

Third-party code and assets keep their own licenses, listed in [third_party/README.md](third_party/README.md).

## Acknowledgements

TerrainGen draws inspiration from existing lunar terrain generation tools:

| Name | Organization | Availability | Capabilities | Limitations |
| --- | --- | --- | --- | --- |
| [synthterrain](https://github.com/NeoGeographyToolkit/synthterrain) | NASA Ames | Open source (Apache-2.0) | Crater and boulder populations from size-frequency distributions, crater profiles and DEMs on flat terrain | Doesn't generate surface detail or upscale existing DEMs, not actively contributed to. |
| [lunasynth](https://github.com/nasa-jpl/lunasynth) | NASA JPL | Open source (Apache-2.0) | Adds craters and boulders to a lunar base DEM, down to 1 cm pixels, and renders images under varying camera and sun positions | Project is not actively-maintained |
| [DLES Terrain Crafting](https://doi.org/10.1109/AERO63441.2025.11068736) | NASA JSC | Not publicly released (paper) | Enhances LOLA and NAC-derived DEMs of the lunar south pole to 20 cm with synthetic craters and boulders, including hand-placed craters from imagery | Not publicly released, relies on manual crater counting, stamps craters from four template shapes, leaves blending rings around craters and smooth interpolated ground between them, and doesn't produce albedo maps. |
| [PANGU](https://pangu.software/) | University of Dundee / STAR-Dundee | Commercial (free for ESA projects) | Multi-resolution planet and asteroid surfaces from real or fractal DEMs with statistical craters and boulders; GPU camera, LiDAR and thermal sensor simulation | Requires a paid license outside ESA projects, and ships as standalone command-line tools and GUIs rather than a library to integrate into a project. |
