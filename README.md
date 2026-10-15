> 🚧 **This project is in active beta development.** This means the API is unstable, features may be added or removed, and breaking changes are likely to occur frequently and without notice as the design is refined.

[![pipeline](https://github.com/alexmaietta/terraingen/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/alexmaietta/terraingen/actions/workflows/ci.yml)
[![release](https://img.shields.io/github/v/release/alexmaietta/terraingen)](https://github.com/alexmaietta/terraingen/releases)
[![pypi](https://img.shields.io/pypi/v/terraingen)](https://pypi.org/project/terraingen/)
[![docs](https://github.com/alexmaietta/terraingen/actions/workflows/docs.yml/badge.svg?branch=main)](https://alexmaietta.github.io/terraingen/)
[![license](https://img.shields.io/badge/License-BSD_3--Clause-blue)](LICENSE)

# TerrainGen

![]()

A high-performance scientific terrain generation tool for interplanetary bodies.

TerrainGen provides a core C++ and Python library designed to easily integrate into external workflows. It is designed with modularity and interoperability in mind. The core requires minimal dependencies and users can extend the base classes for custom functionality. We also provide a command-line interface, a lightweight viewer, a series of example usages, and a standalone demo that integrates TerrainGen into a planetary-scale renderer that synthesizes terrain detail on the GPU, on the fly, while navigating around the Moon.

TerrainGen can

For more information on the concepts behind TerrainGen and the API, see our latest [documentation](https://alexmaietta.github.io/terraingen/).

TerrainGen plugs into downstream simulation and rendering engines such as [Project Chrono](https://github.com/projectchrono/chrono). See [Third-party Integrations and Use Cases](#third-party-integrations-and-use-cases) for current and planned integrations.

TerrainGen is also heavily validated against existing planetary datasets. For more information, see the [validation](https://alexmaietta.github.io/terraingen/validation/) section.

Future work includes extending TerrainGen for other planetary and celestial bodies such as Mars and asteroids.

## Getting Started

To quickly get started with TerrainGen, it is recommended to install the Python package and run the GUI.

```sh
pip install terraingen
terraingen --gui
```

For more information, such as building from source or integrating TerrainGen into your own projects, see our [installation guide]().

## Validation

A large focus of TerrainGen is accuracy, so we put a lot of effort into validating TerrainGen against real planetary imagery and terrains. There are three core methods that we use to do this.

For more information, see our detailed [validation]() documentation.

## Citation

If you use TerrainGen for published research, please cite:

```bibtex
@software{maietta_terraingen,
  author  = {Maietta, Alex},
  title   = {{TerrainGen}: An open-source scientific terrain generation suite for planetary surfaces},
  url     = {https://github.com/alexmaietta/terraingen},
  version = {0.1.0},
  license = {BSD-3-Clause},
  year    = {2026}
}
```

## License

TerrainGen is licensed under the [BSD 3-Clause License](LICENSE).

Third-party code and assets keep their own licenses, listed in [third_party/README.md](third_party/README.md).

## Third-party Integrations and Use Cases

TerrainGen is designed to be integrated into downstream simulation, rendering, and machine learning pipelines. Terrain products from TerrainGen can drive physics simulations or be used to render photorealistic imagery for crater and boulder identification pipelines.

**Simulation integrations**

* [Newton](https://github.com/newton-physics/newton)
* [Nvidia Omniverse](https://www.nvidia.com/en-us/omniverse/)
* [Project Chrono](https://github.com/projectchrono/chrono)
* [IsaacLab](https://isaac-sim.github.io/IsaacLab/)

**Use cases**

* Training crater and boulder identification ML models
* Planetary surface simulations
* Lunar landing optical navigation

## Acknowledgements

TerrainGen draws inspiration from existing lunar terrain generation tools:

| Name | Organization | Availability | Capabilities |
| --- | --- | --- | --- |
| [synthterrain](https://github.com/NeoGeographyToolkit/synthterrain) | NASA Ames | Open source (Apache-2.0) | Crater and boulder populations from size-frequency distributions, crater profiles and DEMs on flat terrain |
| [lunasynth](https://github.com/nasa-jpl/lunasynth) | NASA JPL | Open source (Apache-2.0) | Adds craters and boulders to a lunar base DEM, down to 1 cm pixels, and renders images under varying camera and sun positions |
| [DLES Terrain Crafting](https://doi.org/10.1109/AERO63441.2025.11068736) | NASA JSC | Not publicly released (paper) | Enhances LOLA and NAC-derived DEMs of the lunar south pole to 20 cm with synthetic craters and boulders, including hand-placed craters from imagery |
| [PANGU](https://pangu.software/) | University of Dundee / STAR-Dundee | Commercial | Multi-resolution planet and asteroid surfaces from real or fractal DEMs with statistical craters and boulders; GPU camera, LiDAR and thermal sensor simulation |
