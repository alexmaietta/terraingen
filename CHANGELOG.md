# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
