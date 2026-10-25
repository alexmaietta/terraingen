---
title: TerrainGen
---

# TerrainGen

<figure class="tg-shot" markdown>

![Blue Moon MK1 SN101 lander at Connecting Ridge, with Mons Malapert in the background, split between the source elevation model and TerrainGen's upscaled terrain](images/hero_mk1_sn101_connecting_ridge_split.jpg)

<figcaption>Blue Moon MK1 at Connecting Ridge, near the lunar south pole. Left: the 5 m/px LOLA elevation model. Right: the same terrain upscaled by TerrainGen.</figcaption>

</figure>

Scientific terrain for planetary surfaces: craters, boulders, dunes, layered rock and albedo from published models, on real elevation data or from scratch. From a landing site to the whole Moon, Mars or asteroids.

[Get started](getting-started/index.md){ .md-button .md-button--primary } [Quickstart](getting-started/quickstart.md){ .md-button } [GitHub](https://github.com/alexmaietta/terraingen){ .md-button }
{ .tg-actions }

## Features

- **Sites to whole bodies.** Planes, spheres, triaxial ellipsoids and shape models of irregular bodies, with centimeter-scale detail added to GeoTIFF elevation models or synthetic relief. [Learn more](concepts/surfaces.md)
- **Published crater and boulder models.** Populations follow published size-frequency distributions, and each crater gets a profile, an age, degradation and an irregular rim. [Learn more](features/distributions/index.md)
- **Albedo that ages.** Synthetic albedo maps, with crater ejecta and rays that fade as craters age. [Learn more](features/albedo/index.md)
- **Geologic features.** Secondary and complex craters, boulder ejecta, dunes and ripples, terraced strata and fractured bedrock. [Learn more](features/geology/index.md)
- **Simulation-ready outputs.** GeoTIFF heightmaps and albedo, crater and boulder lists, and terrain and 3D boulder meshes as glTF, OBJ or STL. [Learn more](features/outputs.md)
- **Fast and deterministic.** Noise and crater shaping on the GPU through Vulkan, CUDA or Metal, and the same seed gives the same terrain on any grid or thread count. [Learn more](concepts/gpu.md)

## Installation

Install the Python package and open the GUI:

```sh
pip install terraingenpy
terraingen --gui
```

See [Installation](getting-started/installation.md) for releases, Docker and building from source.

## Where to go next

- [Getting started](getting-started/index.md): install TerrainGen and make a first terrain, step by step.
- [Guides](guides/index.md): how to do specific tasks, such as using the command line, the GUI or the examples.
- [Concepts](concepts/index.md): how TerrainGen is designed, from surfaces and sources to layers and the GPU.
- [Features](features/index.md): each model in detail, with its equations, code and figures.
- [Reference](reference/index.md): the configuration schema, the Python and C++ APIs, the changelog and the bibliography.
