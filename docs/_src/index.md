---
title: TerrainGen
---

# TerrainGen

<figure class="tg-shot" markdown>

![Blue Moon MK1 SN101 lander at Connecting Ridge, with Mons Malapert in the background, split between the source elevation model and TerrainGen's upscaled terrain](images/hero_mk1_sn101_connecting_ridge_split.jpg)

<figcaption>Blue Moon MK1 at Connecting Ridge, near the lunar south pole. The left half is the 5 m/px LOLA elevation model, and the right half is the same terrain upscaled by TerrainGen.</figcaption>

</figure>

TerrainGen makes scientific terrain for planetary surfaces, with craters, boulders, dunes, layered rock and albedo from published models, on real elevation data or from scratch. It covers anything from a landing site to the whole Moon, Mars or asteroids.

[Get started](getting-started/quickstart.md){ .md-button .md-button--primary } [Gallery](gallery/index.md){ .md-button } [GitHub](https://github.com/alexmaietta/terraingen){ .md-button }
{ .tg-actions }

## Features

- **Sites to whole bodies.** Planes, spheres, triaxial ellipsoids and shape models of irregular bodies, with centimeter-scale detail added to GeoTIFF elevation models or synthetic relief. [Learn more](concepts/surfaces.md)
- **Published crater and boulder models.** Populations follow published size-frequency distributions, and each crater gets a profile, an age, degradation and an irregular rim. [Learn more](features/distributions/index.md)
- **Albedo that ages.** Synthetic albedo maps, with crater ejecta and rays that fade as craters age. [Learn more](features/albedo/index.md)
- **Geologic features.** Secondary and complex craters, boulder ejecta, dunes and ripples, terraced strata and fractured bedrock. [Learn more](features/geology/index.md)
- **Customizable throughout.** Replace any part with your own in Python or C++, from sources, layers and upsamplers to size distributions, crater profiles, degradation models and rock meshes, and use it like the built-in ones. [Learn more](guides/workflows/customize-terraingen.md)
- **Simulation-ready outputs.** GeoTIFF heightmaps and albedo, crater and boulder lists, and terrain and 3D boulder meshes as glTF, OBJ or STL. [Learn more](features/outputs.md)
- **Fast and deterministic.** Noise and crater shaping on the GPU through Vulkan, CUDA or Metal, and the same seed gives the same terrain on any grid or thread count. [Learn more](concepts/gpu.md)

## Installation

Install the Python package and open the GUI.

```sh
pip install terraingenpy
terraingen --gui
```

See [Installation](getting-started/installation.md) for releases, Docker and building from source.

## Where to go next

- [Quickstart](getting-started/quickstart.md) makes a first terrain from the command line, then one layer at a time from Python or C++.
- [Guides](guides/index.md) show how to do specific tasks, with the command line and GUI, workflows, and short Python and C++ examples of single tasks.
- [Concepts](concepts/index.md) explains how TerrainGen is designed, from surfaces and sources to layers and the GPU.
- [Features](features/index.md) covers each model in detail, with its equations, code and figures.
- [Reference](reference/index.md) has the configuration schema, the Python and C++ APIs and the bibliography. The [changelog](changelog.md) and how to [cite TerrainGen](citing.md) are under Home.
