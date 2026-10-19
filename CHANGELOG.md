# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Piecewise uniform size-frequency distributions: counts per diameter band, given absolutely or as shares of a total.
- Radial wall marks on craters, ridges and grooves down the wall that are strongest where it is steepest.
- Thomas cluster process for boulder clusters, with Poisson-scattered centres whose clusters add where they overlap.
- Craters on slopes: floors offset downslope and rims and ejecta lowered upslope (Plescia et al. 2019), and faster wear toward a critical gradient (Roering et al. 1999).
- Boulders limited to a maximum slope, optionally rolling down steeper ground to gather at its foot.
- Density maps from GeoTIFFs or constants: a scale, a count in a diameter range, or an areal fraction such as rock abundance, which set crater and boulder densities point by point.
- Size slope maps that bend boulder sizes into a local power law, from an exponent or a mean diameter map.
- Crater and boulder catalogs with a completeness limit, above which they stand in for generated features.
- Configuration reference in the documentation, generated from the schema at build time, and machine-readable value ranges in the schema.
- Rock albedo: boulders brighter than the regolith (Holt 1981; Marshal et al. 2023), and a speckle of the share of each sample covered by boulders too small to resolve, with the scatter of randomly placed rocks.
- Slope albedo: steep ground kept immature by mass wasting, its optical maturity rising with slope beyond 20 degrees (Ikeda et al. 2022; Lucey et al. 2014).

### Changed

- Density maps that count every feature take out the secondaries thrown from a layer's craters and the blocks thrown out around craters, and boulder patches and bedrock keep a map's mean, so nothing is counted twice.
- Boulder models settle onto a stable face by default instead of balancing on an edge or corner.
- Boulder generation logs under boulders rather than craters.
- Crater and boulder generation summaries log at debug level; `terraingen` ends with one line counting the files written instead of listing each, and feature outputs log how long they took.
- Generation logs each grid as it is evaluated, with the time of every layer, the albedo and the materials, and ends with each step's total and share.
- Faster boulders: the ground beneath them is read in one batch, outputs over one region share the boulders placed for the first, models are placed in parallel, ejecta craters are found through a spatial index and shapes skip the roughness of fracture planes that cannot reach a vertex. The hero demo runs in about 10 s instead of 276 s.
- Martin, Parkes and Dunstan is the default crater profile.

### Fixed

- Freshness degradation never makes a crater deeper than fresh: basins shallower than the cutoff keep their fresh shape.
- Small craters inside larger ones on sloping ground no longer stand as raised mounds: datums are fitted to the slope-shaped relief that is applied.

## [0.1.0] - 2026-10-18

Initial release.

### Added

- C++20 library with Python bindings (`terraingenpy` on PyPI) for generating terrain from scratch or adding detail to existing DEMs.
- Craters and boulders from published size-frequency distributions, with crater ages, profiles, degradation, irregularity, secondaries and ejecta.
- Procedural 3D boulder models, and small boulders baked into the heightmap.
- Synthetic albedo with crater ejecta and rays that fade with age.
- Geologic features: dunes and ripples, layered strata, fractured bedrock, ventifacts and sand pooled in lows or behind boulders.
- Planes, spheres and ellipsoids, whole-body meshes, tiles and point queries.
- Deterministic output for a seed on any grid, tile or thread count.
- GPU acceleration on Vulkan, CUDA and Metal, detected at run time.
- `terraingen` command-line interface driven by JSON configurations.
- Examples in C++, Python and JSON.
- Release archives for Linux, macOS and Windows, Debian packages, and a Docker image.

[Unreleased]: https://github.com/alexmaietta/terraingen/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/alexmaietta/terraingen/releases/tag/v0.1.0
