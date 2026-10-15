> 🚧 **This project is in active beta development.** This means the API is unstable, features may be added or removed, and breaking changes are likely to occur frequently and without notice as the design is refined.

[![pipeline](https://github.com/alexmaietta/terraingen/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/alexmaietta/terraingen/actions/workflows/ci.yml)
[![release](https://img.shields.io/github/v/release/alexmaietta/terraingen)](https://github.com/alexmaietta/terraingen/releases)
[![pypi](https://img.shields.io/pypi/v/terraingen)](https://pypi.org/project/terraingen/)
[![docs](https://github.com/alexmaietta/terraingen/actions/workflows/docs.yml/badge.svg?branch=main)](https://alexmaietta.github.io/terraingen/)
[![license](https://img.shields.io/badge/License-BSD_3--Clause-blue)](LICENSE)

# TerrainGen

![]()

A high-performance scientific terrain generation tool for interplanetary bodies.

TerrainGen can:

- Generate synthetic terrain from scratch, or add fine detail to existing elevation models.
- Populate craters and boulders from cumulative size-frequency distributions.
- Model each crater's profile, age, degradation, irregular outline and secondary craters.
- Bake boulders into the heightmap or place them as 3D models.
- Produce synthetic albedo maps with crater ejecta and rays that fade with crater age.
- Run on the GPU, and give the same terrain for the same seed on any grid, tile or thread count.

We provide a core C++ and Python library designed to easily integrate into external workflows including simulations. It is designed with modularity and interoperability in mind. The core requires minimal dependencies and users can extend the base classes for custom functionality. We also provide a command-line interface, a lightweight viewer, a series of example usages, and a standalone demo that integrates TerrainGen into a planetary-scale renderer that synthesizes terrain detail on the GPU, on the fly, while navigating around the Moon.

We also provide examples or full integrations into existing third-party simulation tools such as Project Chrono, Newton, IsaacLab, and NASA Trick.

For more information on the concepts behind TerrainGen and the API, see our latest [documentation](https://alexmaietta.github.io/terraingen/).

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

## Acknowledgements

TerrainGen draws inspiration from existing lunar terrain generation tools:

| Name | Organization | Availability | Capabilities | Limitations |
| --- | --- | --- | --- | --- |
| [synthterrain](https://github.com/NeoGeographyToolkit/synthterrain) | NASA Ames | Open source (Apache-2.0) | Crater and boulder populations from size-frequency distributions, crater profiles and DEMs on flat terrain | Doesn't generate surface detail or upscale existing DEMs, not actively contributed to. |
| [lunasynth](https://github.com/nasa-jpl/lunasynth) | NASA JPL | Open source (Apache-2.0) | Adds craters and boulders to a lunar base DEM, down to 1 cm pixels, and renders images under varying camera and sun positions | Project is not actively-maintained |
| [DLES Terrain Crafting](https://doi.org/10.1109/AERO63441.2025.11068736) | NASA JSC | Not publicly released (paper) | Enhances LOLA and NAC-derived DEMs of the lunar south pole to 20 cm with synthetic craters and boulders, including hand-placed craters from imagery | Not publicly released, |
| [PANGU](https://pangu.software/) | University of Dundee / STAR-Dundee | Commercial | Multi-resolution planet and asteroid surfaces from real or fractal DEMs with statistical craters and boulders; GPU camera, LiDAR and thermal sensor simulation | Commercial software |
