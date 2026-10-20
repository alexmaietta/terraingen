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
- Crater shaping and datum fitting on the GPU on spheres, ellipsoids and shape models, not only on planes.
- Configurations are checked against every field's documented range, and errors name the exact field, such as `layers[2].reach`.
- Python options classes document every field with its description and unit, and gain `validate()` and a readable `repr`.
- `RasterSource`, heights from a raster in memory, shared rather than copied, low-passed and resampled as a GeoTIFF source is; `GeoTiffSource` is now a `RasterSource` of its band.
- Windowed GeoTIFF reads that decode only the strips or tiles a window meets, and a `region` for GeoTIFF sources that reads only the window it needs while giving the same heights there as the whole file.
- `Source::ResolutionAt`, the cell size of the data behind each height, and `UnresolvedCraterLimit`, a completeness limit that leaves the craters a DEM already resolves to it and generates only smaller ones; `MinimumField` combines it with a catalog's limit.
- Midpoint displacement layer: random midpoint displacement (diamond-square) expansion of a DEM or any base lattice to finer spacings, as PANGU expands DEMs (Martin et al. 2014), keeping every coarser node so nested grids share their cells, with the scale and fractal dimension estimated from the base when not given.
- Spectral continuation layer: detail finer than a DEM that continues its fitted power-law spectrum (radially averaged, Hann-windowed PSD) to shorter wavelengths as sparse Gabor noise, tapered in at the DEM's Nyquist wavelength so resolved scales gain nothing; an alternative to midpoint displacement without lattice artefacts, with grids, tiles and points in agreement.
- Roughness and noise layers that estimate their settings from the base with `estimate_from_base`: the Hurst exponent and RMS height difference of its variogram at its native cells, extrapolated to the roughness baseline or matched at each noise layer's wavelength; explicit values override the estimates.
- Manual craters with an explicit rim-to-floor depth and rim height, in place of what the degradation model and age give; diffusion profiles solve for the age that reaches the depth.
- Manual boulders that fix any of their height, burial, yaw, lean, tilt, rest, wear and shape seed; unset fields are drawn as for generated boulders, and fixed ones are neither re-settled nor broken.
- Terrain meshes take `cutouts`, a `skirt` and a frame `site`.
- Configuration keys `region` (`min_x`, `min_y`, `max_x`, `max_y` in CRS units) on raster bases and `dem_detail` layers to read only a window of a DEM, and `unresolved_pixels_across` on crater layers to generate only the craters the base DEM does not already resolve, combined with any catalog `completeness`.
- Manual-only boulder layers: `generate_random` off (`BoulderLayerOptions::generateRandom`) places only the `manual` boulders, with no random boulders or crater blocks, draws no candidates and needs no valid sizes or coverage; `CraterLayer::Manual` and `BoulderLayer::Manual` (Python `CraterLayer.manual`, `BoulderLayer.manual`) make layers of only the given craters or boulders.

### Changed

- Breaking: every angle key in configurations and the Python options classes is now in degrees and ends in `_deg` (`wind_azimuth_deg`, `slip_face_angle_deg`, `heading_deg`, `crease_angle_deg`), azimuths counter-clockwise from +x (local east); the old keys are rejected. The rest of the Python API follows: `SyntheticAlbedo(hue_deg=, hue_noise_deg=)`, `hsv_to_rgb(hue_deg)`, `write_models(crease_angle_deg=)`, `TriangleMesh.with_creases(crease_angle_deg)`, `RigidTransform.rotation_x/y/z(angle_deg)`, `CubeSphereCells.direction(face, alpha_deg, beta_deg)`, `SlopeAlbedo.maturity_at(slope_deg)`, `Boulder` `yaw_deg`, `lean_deg`, `tilt_deg` and `tilt_azimuth_deg` (also the boulder table columns and the columns of boulder lists written as CSV or JSON), `BoulderFacet` `azimuth_deg` and `angle_deg`, `CraterImpact` `angle_deg` and `heading_deg`, `CraterOutline.heading_deg` and `RayDirection.azimuth_deg`; the C++ library keeps radians.
- Defaults give a plausible lunar terrain at 1 m per pixel in which every height layer shows: craters from a production surface capped below the Trask equilibrium with steady-state ages, diffusion and irregular outlines; Surveyor boulders; noise relief scaled to its wavelength; and visible roughness, bedforms and dunes.
- Boulder layers default to the nominal NASA SP-8023 intercrater sizes rather than Golombek-Rapp; a `coverage`, `qa`, `qb` or `ejecta.coverage` that no Golombek-Rapp sizes use is now an error that names `"sizes": {"type": "golombek_rapp"}`.
- Boulder lists and models hold every boulder of the layer's diameter range; only the heightmap leaves out boulders too small to resolve.
- Boulder ejecta come from every crater layer before the boulder layer, not only the last one.
- The boulder layer's `exposure_spacing` key is now `ground_spacing` (Python `ground_spacing`), the spacing of every ground read the layer makes.
- Crater shaping has one implementation, the kernel, on every surface; craters on bodies and custom profiles may differ from before in the last digits.
- Random streams and hashes are unified, so boulder, albedo, dune, bedform and pavement outputs for a given seed differ from earlier versions; their statistics are unchanged.
- Every evaluation defaults to all threads, including `EvaluateAt`, `EvaluateTile` and Python `evaluate_tile`.
- The `materials` module is folded into `pipeline` (material maps) and `albedo` (Hapke photometry); include `terraingen/pipeline/materials.h` and `terraingen/albedo/photometry.h`.
- Python: `ExposureField.exposure` takes `(x, y, spacing=0.0, terrain=None, layers=0)` like the other ground queries, unbounded options read as `None` instead of infinity, `write_outputs` returns paths, a subclass missing a required method raises `TypeError`, and `CraterFeatures.write` is `write_list`.
- Python names: `cell_of` is `cell_at`, `point_of` is `coordinates`, `level_for` is `resolving_level`, `outline_of` is `irregular_outline`, `min_diameter_for_count` is `count_limit_diameter`, `singer_range_for_size` is `singer_max_range`, `SplotchOptions` is `SplotchAlbedoOptions` and `SlopeMaturityOptions` is `SlopeAlbedoOptions`.
- The CI coverage job builds at `-O1` without atomic counters, so it caches and runs several times faster.

- Density maps that count every feature take out the secondaries thrown from a layer's craters and the blocks thrown out around craters, and boulder patches and bedrock keep a map's mean, so nothing is counted twice.
- Boulder models settle onto a stable face by default instead of balancing on an edge or corner.
- Boulder generation logs under boulders rather than craters.
- Crater and boulder generation summaries log at debug level; `terraingen` ends with one line counting the files written instead of listing each, and feature outputs log how long they took.
- Generation logs each grid as it is evaluated, with the time of every layer, the albedo and the materials, and ends with each step's total and share.
- Faster boulders: the ground beneath them is read in one batch, outputs over one region share the boulders placed for the first, models are placed in parallel, ejecta craters are found through a spatial index and shapes skip the roughness of fracture planes that cannot reach a vertex. The hero demo runs in about 10 s instead of 276 s.
- Martin, Parkes and Dunstan is the default crater profile.
- Breaking: crater layers' `generate` is now `generate_random` (`CraterLayerOptions::generateRandom`, Python `generate_random`): it draws random craters from the size distribution, and manual craters are placed either way; the old key is rejected.

### Fixed

- Freshness degradation never makes a crater deeper than fresh: basins shallower than the cutoff keep their fresh shape.
- Small craters inside larger ones on sloping ground no longer stand as raised mounds: datums are fitted to the slope-shaped relief that is applied.
- Crater layers over other layers read the ground beneath their slopes and datums in batches rather than point by point, and point batches are evaluated in spatial order: the hero demo went from 433 s to 11 s with the same output.
- Secondary craters run on the GPU backend the terrain asks for instead of always on the CPU.
- A crater layer after a boulder layer no longer refits crater datums for every batch of ground reads.
- Tilted boulders are no longer cut off at tile edges and point queries.
- Crater and boulder lists honour a spacing finer than the grid.
- Zero-area regions and crater sizes below the models' range are rejected with a clear error instead of failing during generation.

### Removed

- Tile schemes, tiles and the tile cache (`TileScheme`, `TileKey`, `EvaluateTile`, `EvaluateTiles`, `TileCache`, in C++ and Python): level of detail belongs to the renderer, and any grid evaluated alone already equals the same cells of a larger one. The tile examples go too; the point query examples move to a `queries` category.
- Python `crater_blend_weight` (use `CraterBlend.weight`), `CraterAgeModel.lifetime_gyr` (use `lifetime_at`) and the flat selection aliases on crater irregularity, crater albedo and secondary options (use `selection`).

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
