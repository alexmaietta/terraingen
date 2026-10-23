# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.2.0] - 2026-10-23

Sources replace the base: DEMs are upsampled rather than resampled, synthetic sources generate heights from noise, and layers leave out what the source already resolves. Outputs set their own pixel size and extent, `GenerateFromConfig` returns everything it computes, and the core types are renamed. Configurations and code written for 0.1.0 need the changes under [Migrating from 0.1.0](#migrating-from-010).

### Added

- Sources in configurations (`source`) of type `raster`, `chain` or `synthetic`; without one the terrain starts from a flat synthetic source at 0 m.
- `SyntheticSource` (source `synthetic`): heights from a noise stack plus `elevation_m` over an unbounded plane or the whole body, evaluated at every point, or sampled at posts `pixel_size_m` apart and upsampled like a DEM; an empty stack is flat.
- `RasterSource`, heights from a raster in memory, shared rather than copied and upsampled as a GeoTIFF source is; `GeoTiffSource` is a `RasterSource` of its band.
- Upsampling on sources (`Upsampler`, config `upsampling`): how a source's posts become heights between them and at any finer sample spacing, deterministically, so points and grids at one spacing agree; kernels `nearest`, `bilinear`, `cubic` (the default), `catmull_rom`, `lanczos3` and `cubic_bspline`.
- Diamond-square upsampling (`DiamondSquareUpsampler`, `"upsampling": {"type": "diamond_square"}`): random midpoint displacement of a source's posts to finer spacings, as PANGU expands DEMs (Martin et al. 2014), keeping every coarser node, with bilinear or Catmull-Rom (`interp`) refinement and the scale and fractal dimension estimated from the posts when not given.
- `RegisterUpsampler` (Python `register_upsampler`) names upsampler types of your own, C++ or Python subclasses of `Upsampler`, for configurations to use with their settings.
- Windowed GeoTIFF reads that decode only the strips or tiles a window meets, and `crop` (`min_x`, `min_y`, `max_x`, `max_y` in CRS units) on raster sources to read only the window needed while giving the same heights there as the whole file.
- `effective_pixel_size_m` on raster sources (`effectivePixelSize`), the cell size a DEM upsampled or smoothed before it was saved truly resolves.
- `Source::ResolutionAt`, the cell size of the data behind each height, and `UnresolvedCraterLimit`, a completeness limit that leaves the craters a DEM already resolves to it; `MinimumField` combines it with a catalog's limit.
- `source_resolves_px` on craters, secondaries, boulders, noise, dunes and bedforms: features at least that many source cells across, by diameter, wavelength or dune width, are already in the source, so only smaller ones are added at each point, each raster of a chain giving its own limit; ignored on synthetic sources, `null` adds them all. In C++ `sourceResolvesPixels` on the layer options, applied when the layer is given its `source`, with `SourceResolvedLimit` and `LayerResolvedLimit` (Python `source_resolved_limit`).
- Spectral continuation layer (`spectral_continuation`, `SpectralContinuationLayer`): detail finer than a DEM that continues its fitted power-law spectrum to shorter wavelengths as sparse Gabor noise, tapered in at the DEM's Nyquist wavelength so resolved scales gain nothing, with grids and points in agreement.
- Roughness and noise layers that estimate their settings from the source with `estimate_from_base`: the Hurst exponent and RMS height difference of its variogram, extrapolated to the roughness baseline or matched at each noise layer's wavelength; explicit values override the estimates. The estimators are public: `EstimateSourceRoughness`, `EstimateBaseRoughness`, `EstimatePowerLawSpectrum`, `RadialPowerSpectrum`, `EstimateMidpointRoughness` and `raster/variogram.h` (Python `estimate_source_roughness` and the rest).
- Lanczos (three lobes), Catmull-Rom and cubic B-spline kernels for `Resample`, `SampleRaster` and `ResampleMethod`.
- Output areas: `pixel_size_m` and `extent_m` (`extent_deg` on a body) as `{"center": [x, y], "size": [w, h]}` or `{"min": [x, y], "max": [x, y]}`, defaulting to a raster or chain source's extent and cell size; the grid starts at the extent's min corner, is snapped outward to whole cells and sampled at cell centers (`OutputAreaSpec`, `ExtentSpec`, `config::SnappedGrid`).
- Optional output paths: without a `path` the result is returned and nothing is written. `GenerateFromConfig` returns everything the outputs ask for, each computed once: `GeneratedGrid` holds the grid, heights, albedo, materials, photometry, placed craters and boulders and terrain mesh, `GeneratedOutputs` the body mesh or ICQ model, the report, `Grid()` and `Find()`; Python `GeneratedGrid` values are views without a copy.
- `terrain_mesh` outputs take `cutouts` (`{"min": [x, y], "max": [x, y]}` boxes), a `skirt_m` and a frame `site_deg` as [lon, lat].
- Terrain reports: output type `report` (`ReportOutput`) with an optional `path`, returned as `GeneratedOutputs::report` and written as JSON by `WriteOutputs`; per grid (`GridReport`) height and slope statistics, craters per power-of-2 diameter bin against the first crater layer's density and boulder counts and cover, with per-layer timings, backend, threads, layer seeds and version (`ReportGrid`, `ReportToJson`, `TerrainReport`, `LayerTimer`; Python `report_grid`, `report_to_json`, `TerrainReport.to_dict`).
- `GetMeshSize` and `FillMesh` (Python `get_mesh_size`, `fill_mesh`) size and write a height mesh into caller buffers laid out as Chrono's ChVector3d and ChVector3i arrays or NumPy (N, 3) float64 and (M, 3) int32 arrays, with no copy; `FillHeights` (Python `fill_heights`) evaluates into a caller buffer; `TriangleMesh.positions` and `TriangleMesh.triangles` are NumPy views without a copy.
- Piecewise uniform size-frequency distributions (`piecewise_uniform`): counts per diameter band, given absolutely or as shares of a total.
- Piecewise value distributions, uniform within bins drawn by weight, and `freshness` crater ages that draw each crater's freshness from a value distribution.
- Radial wall marks on craters, ridges and grooves down the wall that are strongest where it is steepest.
- Craters on slopes: floors offset downslope and rims and ejecta lowered upslope (Plescia et al. 2019), and faster wear toward a critical gradient (Roering et al. 1999).
- Manual craters with an explicit rim-to-floor depth and rim height in place of what the degradation model and age give; diffusion profiles solve for the age that reaches the depth.
- Thomas cluster process for boulder clusters, with Poisson-scattered centers whose clusters add where they overlap.
- Boulders limited to a maximum slope, optionally rolling down steeper ground to gather at its foot.
- Manual boulders that fix any of their height, burial, yaw, lean, tilt, rest, wear and shape seed; unset fields are drawn as for generated boulders, and fixed ones are neither re-settled nor broken.
- Manual-only layers: boulder layers with `generate_random` off place only their `manual` boulders, and `CraterLayer::Manual` and `BoulderLayer::Manual` (Python `CraterLayer.manual`, `BoulderLayer.manual`) make layers of only the given craters or boulders.
- Density maps from GeoTIFFs or constants: a scale, a count in a diameter range, or an areal fraction such as rock abundance, which set crater and boulder densities point by point.
- Size slope maps that bend boulder sizes into a local power law, from an exponent or a mean diameter map.
- Crater and boulder catalogs with a completeness limit, above which they stand in for generated features.
- Rock albedo (`rocks`): boulders brighter than the regolith (Holt 1981; Marshal et al. 2023), and a speckle of the share of each sample covered by boulders too small to resolve.
- Slope albedo (`slopes`): steep ground kept immature by mass wasting, its optical maturity rising with slope beyond 20 degrees (Ikeda et al. 2022; Lucey et al. 2014).
- Crater shaping and datum fitting on the GPU on spheres, ellipsoids and shape models, not only on planes.
- `fade_px` and `fade_shape` on crater `apply` and boulder layers: the resolution fade as its start and end output samples and its curve.
- Configurations are checked against every field's documented range, and errors name the exact field, such as `layers[2].reach`.
- Configuration reference in the documentation, generated from the schema at build time, and machine-readable value ranges in the schema.
- Python options classes document every field with its description and unit, and gain `validate()` and a readable `repr`.

### Changed

- Breaking: the configuration's `base` is now `source`, and the region layer is now `masked`; old keys and types are rejected.
- Breaking: raster sources upsample instead of resampling: a raster source's grid is always its file's, and evaluating it over a finer grid gives finer cells; `RasterSourceOptions` and `GeoTiffSourceOptions` take an `upsampler` and an `effectivePixelSize`.
- Breaking: outputs give their area as `pixel_size_m` and `extent_m` in place of `region` (`width`, `height`, `pixel_size`, `centre_x`, `centre_y`) and of `spacing` on crater lists, boulder lists and boulder models; both are required without a raster or chain source.
- Breaking: on a raster or chain source, craters, secondaries, dunes and bedforms leave out by default what is at least 4 source cells across (`source_resolves_px`); set it to `null` for the 0.1.0 output.
- Breaking: every angle key in configurations and the Python options classes is in degrees and ends in `_deg`, azimuths counter-clockwise from +x (local east), and the rest of the Python API follows with `_deg` names and arguments; the C++ library keeps radians.
- Breaking: crater and boulder size keys carry their units (`_m`, `_px`), in configurations and the Python options classes alike, and crater layers' `generate` is now `generate_random`; the old keys are rejected.
- Breaking: core types and functions are renamed with no aliases, `Terrain` to `TerrainGenerator`, `GenerateTerrain` to `GenerateFromConfig`, `TerrainConfig` to `TerrainGenConfig`, `HeightMesh` to `BuildMesh`, `RegionLayer` to `MaskedLayer` and the rest in the table below, and Python names follow C++ names one to one.
- Breaking: sources take the sample spacing: `Source::Fill(heights, spacing)` and `Source::HeightAt(x, y, spacing)`, and Python `Source` subclasses define `height_at(x, y, spacing)` and `fill(heights, spacing)`; a source without bounds gives a grid without cells (`BoundedGrid`), and `Evaluate(generator)` needs a grid for it.
- The library, Python bindings, tests and examples are C++17 (0.1.0 needed C++20); public headers take `Span` in place of `std::span`.
- Defaults give a plausible lunar terrain at 1 m per pixel in which every height layer shows: craters from a production surface capped below the Trask equilibrium with steady-state ages, diffusion and irregular outlines, Martin, Parkes and Dunstan profiles, nominal NASA SP-8023 intercrater boulders (0.4% cover by rocks of 1 m and more), noise relief scaled to its wavelength, and visible roughness, bedforms and dunes.
- A boulder `coverage`, `qa`, `qb` or `ejecta.coverage` that no Golombek-Rapp sizes use is an error that names `"sizes": {"type": "golombek_rapp"}`.
- Boulder lists and models hold every boulder of the layer's diameter range; only the heightmap leaves out boulders too small to resolve.
- Boulder ejecta come from every crater layer before the boulder layer, not only the last one.
- Boulder models settle onto a stable face by default instead of balancing on an edge or corner.
- Density maps that count every feature take out the secondaries and blocks thrown from a layer's craters, and boulder patches and bedrock keep a map's mean, so nothing is counted twice.
- The `matched` diffusivity wears each crater to Fassett et al.'s visibility cutoff at the end of its equilibrium lifetime, for the layer's own profile.
- Crater shaping has one implementation, the kernel, on every surface; craters on bodies and custom profiles may differ from 0.1.0 in the last digits.
- Random streams and hashes are unified, so boulder, albedo, dune, bedform and pavement outputs for a given seed differ from 0.1.0; their statistics are unchanged.
- Every evaluation defaults to all threads, including `HeightAt` and `HeightsAt`.
- The `materials` module is folded into `pipeline` (material maps, `terraingen/pipeline/materials.h`) and `albedo` (Hapke photometry, `terraingen/albedo/photometry.h`), and `NoiseLayersAt` moves to `terraingen/sources/synthetic_terrain.h`.
- Python: `ExposureField.exposure` takes `(x, y, spacing=0.0, generator=None, layers=0)` like the other ground queries, unbounded options read as `None` instead of infinity, `write_outputs` returns paths and a subclass missing a required method raises `TypeError`.
- Written configurations leave out lists that are empty by default.
- Faster craters and boulders: ground reads for crater slopes and datums and beneath boulders are batched and spatially ordered, outputs over one area share the boulders placed for the first, boulder models are placed in parallel and ejecta craters found through a spatial index; the hero demo went from 433 s to about 10 s.
- Logs: generation logs each grid as it is evaluated with the time of every layer, the albedo and the materials and ends with each step's total and share; crater and boulder summaries log at debug level, boulders under boulders rather than craters, and `terraingen` ends with one line counting the files written.

### Removed

- Tile schemes, tiles and the tile cache (`TileScheme`, `TileKey`, `EvaluateTile`, `EvaluateTiles`, `TileCache`, in C++ and Python): level of detail belongs to the renderer, and any grid evaluated alone already equals the same cells of a larger one; the point query examples move to a `queries` category.
- Resampling and low-passing raster sources: `pixelSize`, `resampling` and `lowPass` on `GeoTiffSourceOptions` and the config keys `pixel_size`, `resampling` and `low_pass` on raster sources; upsampling over a finer grid replaces them.
- `DetailLayer` and the `dem_detail` layer, which only restored the detail a low-passed source removed.
- `GridRegionSpec`, `config::RegionGrid` and `config::FeatureOutput`; an output's area is an `OutputAreaSpec`.
- Python `crater_blend_weight` (use `CraterBlend.weight`), `CraterAgeModel.lifetime_gyr` (use `lifetime_at`) and the flat selection aliases on crater irregularity, crater albedo and secondary options (use `selection`).

### Fixed

- Freshness degradation never makes a crater deeper than fresh: basins shallower than the cutoff keep their fresh shape.
- Small craters inside larger ones on sloping ground no longer stand as raised mounds: datums are fitted to the slope-shaped relief that is applied.
- Secondary craters run on the GPU backend the terrain asks for instead of always on the CPU.
- A crater layer after a boulder layer no longer refits crater datums for every batch of ground reads.
- Tilted boulders are no longer cut off at grid edges and in point queries.
- Zero-area output areas and crater sizes below the models' range are rejected with a clear error instead of failing during generation.
- The library builds without warnings under Clang and MSVC.

### Migrating from 0.1.0

Old keys and names are rejected rather than aliased. Angle keys are in degrees; sizes in meters unless they end in `_px`.

**Configuration, top level and sources**

| 0.1.0 | 0.2.0 |
| --- | --- |
| `base` | `source` (types `raster`, `chain`, `synthetic`) |
| `base.pixel_size`, `base.resampling` | `source.upsampling`; set the output's `pixel_size_m` for finer cells |
| `base.low_pass` | removed; `effective_pixel_size_m` for a DEM smoothed before it was saved |
| layer `dem_detail` | removed; use `source.upsampling` or a `spectral_continuation` layer |

**Configuration, outputs**

| 0.1.0 | 0.2.0 |
| --- | --- |
| `region.pixel_size` | `pixel_size_m` |
| `region.width`, `height`, `centre_x`, `centre_y` | `extent_m: {"center": [x, y], "size": [width * pixel_size, height * pixel_size]}` (`extent_deg` on a body) |
| `spacing` on `crater_list`, `boulder_list`, `boulder_models` | `pixel_size_m` |
| `path` required | `path` optional |
| `crease_angle` (`boulder_models`) | `crease_angle_deg` |

**Configuration, layers**

| 0.1.0 | 0.2.0 |
| --- | --- |
| layer `region` | layer `masked` |
| craters `generate` | `generate_random` |
| craters `min_diameter`, `max_diameter` | `min_diameter_m`, `max_diameter_m` |
| craters `apply.fade` {`start_samples`, `end_samples`, `shape`} | `apply.fade_px` [start, end], `apply.fade_shape` |
| crater `irregularity.heading`, `heading_spread` | `heading_deg`, `heading_spread_deg` |
| secondaries `min_diameter`, `max_diameter`, `min_primary_diameter`, `max_primary_diameter` | the same with `_m` |
| secondaries `width_degrees`, `min_half_angle_degrees` | `width_deg`, `min_half_angle_deg` |
| boulders `min_diameter`, `max_diameter` | `min_diameter_m`, `max_diameter_m` |
| boulders `fade` {`start_samples`, `end_samples`, `shape`} | `fade_px` [start, end], `fade_shape` |
| boulders `exposure_spacing` | `ground_spacing_m` |
| boulders `max_lean`, `max_tilt` | `max_lean_deg`, `max_tilt_deg` |
| boulders `patches.size`, `clusters.spacing`, `clusters.radius` | `patches.size_m`, `clusters.spacing_m`, `clusters.radius_m` |
| boulders `ejecta.min_crater_diameter`, `ejecta.regolith_thickness` | `ejecta.min_crater_diameter_m`, `ejecta.regolith_thickness_m` |
| boulders `fragmentation.min_diameter`, `relief_range.min_diameter`, `relief_range.max_diameter` | the same with `_m` |
| boulders `models.min_diameter`, `models.max_diameter`, `models.min_samples`, `models.max_samples` | `models.min_diameter_m`, `models.max_diameter_m`, `models.min_px`, `models.max_px` |
| manual boulders `yaw`, `lean`, `tilt`, `tilt_azimuth` | `yaw_deg`, `lean_deg`, `tilt_deg`, `tilt_azimuth_deg` |
| ventifacts `wind_azimuth`, `second_wind_azimuth`, `min_angle`, `max_angle` | the same with `_deg` |
| dunes `wind_azimuth`, `azimuth_spread`, `slip_face_angle`, `stoss_slope`, `flank_slope` | the same with `_deg` |
| bedforms `wind_azimuth`, `azimuth_spread` | `wind_azimuth_deg`, `azimuth_spread_deg` |
| strata `dip`, `dip_azimuth` | `dip_deg`, `dip_azimuth_deg` |
| pavement `max_tilt` | `max_tilt_deg` |
| sand shadow `wind_azimuth`, `flank_angle` | `wind_azimuth_deg`, `flank_angle_deg` |
| albedo `hue`, `hue_noise`, `heading` | `hue_deg`, `hue_noise_deg`, `heading_deg` |
| albedo `*_degrees` (`width_degrees`, `streak_width_degrees`, `jet_angle_degrees`, `steep_slope_degrees` and the rest) | `*_deg` |
| materials `roughness` | `roughness_deg` |

**C++ API**

| 0.1.0 | 0.2.0 |
| --- | --- |
| `Terrain` (`pipeline/terrain.h`) | `TerrainGenerator` (`pipeline/generator.h`) |
| `EvaluateAt`, `EvaluatePoints` | `HeightAt`, `HeightsAt` |
| `ValidateTerrain`, `BuildTerrain` | `ValidateGenerator`, `BuildGenerator` |
| `GenerateTerrain`, `GeneratedTerrain` (`.terrain`) | `GenerateFromConfig`, `GeneratedOutputs` (`.generator`) |
| `TerrainConfig` | `TerrainGenConfig` |
| `ConfigSummary::base` | `ConfigSummary::source` |
| `HeightMesh`, `HeightMeshOptions` (`pipeline/height_mesh.h`) | `BuildMesh`, `BuildMeshOptions` (`pipeline/mesh_build.h`) |
| `RegionLayer` (`layers/region.h`), `RegionShape::centre` | `MaskedLayer` (`layers/masked.h`), `RegionShape::center` |
| `Source::Fill(heights)`, `Source::HeightAt(x, y)` | `Source::Fill(heights, spacing)`, `Source::HeightAt(x, y, spacing)` |
| `GeoTiffSourceOptions::pixelSize`, `resampling`, `lowPass` | `upsampler`, `effectivePixelSize`; evaluate over a finer grid |
| `CraterLayerOptions::generate` | `CraterLayerOptions::generateRandom` |
| `BoulderLayerOptions::exposureSpacing` | `BoulderLayerOptions::groundSpacing` |
| `DetailLayer`, `TileScheme`, `TileKey`, `TileCache`, `EvaluateTile`, `EvaluateTiles` | removed |
| `GridRegionSpec`, `config::RegionGrid`, `config::FeatureOutput` | `OutputAreaSpec`, `ExtentSpec`, `config::SnappedGrid` |
| `terraingen/materials/` | `terraingen/pipeline/materials.h`, `terraingen/albedo/photometry.h` |
| `NoiseLayersAt` in `layers/noise_relief.h` | `sources/synthetic_terrain.h` |
| `TERRAINGEN_API_VERSION` 1 | 2 |

**Python API**

| 0.1.0 | 0.2.0 |
| --- | --- |
| `Terrain` | `TerrainGenerator` |
| `evaluate_at`, `evaluate_points` | `height_at`, `heights_at` |
| `build_terrain`, `generate_terrain`, `GeneratedTerrain` | `build_generator`, `generate_from_config`, `GeneratedOutputs` |
| `Config`, `Config.from_json`, `Config.load`, `Config.to_json` | `TerrainGenConfig`, `parse_config`, `load_config`, `config_to_json` |
| `Config.base_grid` | `TerrainGenConfig.source_grid` |
| `height_mesh` | `build_mesh` |
| `RegionLayer` | `MaskedLayer` |
| keyword `terrain=` | `generator=` (and `generated=` for `write_outputs`) |
| `Source.height_at(x, y)`, `Source.fill(heights)` | `height_at(x, y, spacing)`, `fill(heights, spacing)` |
| `GeoTiffSource(pixel_size=, resampling=, low_pass=)` | `GeoTiffSource(upsampler=, effective_pixel_size=, crop=)` |
| `version`, `api_version`, `build_info` | `get_version`, `get_api_version`, `get_build_info` |
| `boulder_fillet_height`, `perlin_noise3` | `fillet_height`, `perlin_noise3_precise` |
| `TriangleMesh.subdivided` | `TriangleMesh.subdivide` |
| `Raster.nodata`, `GeoTiffInfo.nodata` | `Raster.no_data`, `GeoTiffInfo.no_data` |
| `ValueDistributionKind` | `ValueDistribution.Kind` |
| `CraterGenerationOptions.min_diameter`, `max_diameter` | `d_min`, `d_max` |
| `CraterFeatures.write` | `CraterFeatures.write_list` |
| `cell_of`, `point_of`, `level_for`, `outline_of` | `cell_at`, `coordinates`, `resolving_level`, `irregular_outline` |
| `min_diameter_for_count`, `singer_range_for_size` | `count_limit_diameter`, `singer_max_range` |
| `SplotchOptions`, `SlopeMaturityOptions` | `SplotchAlbedoOptions`, `SlopeAlbedoOptions` |
| angles in radians (`SyntheticAlbedo(hue=)`, `hsv_to_rgb(hue)`, `RigidTransform.rotation_x(angle)`, `Boulder.yaw`, ...) | degrees with `_deg` names (`hue_deg=`, `angle_deg`, `Boulder.yaw_deg`, ...) |
| `crater_blend_weight`, `CraterAgeModel.lifetime_gyr` | `CraterBlend.weight`, `lifetime_at` |
| `TileScheme`, `TileKey`, `TileCache`, `evaluate_tile`, `evaluate_tiles` | removed |

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

[Unreleased]: https://github.com/alexmaietta/terraingen/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/alexmaietta/terraingen/compare/v0.1.0...v0.2.0
[0.1.0]: https://github.com/alexmaietta/terraingen/releases/tag/v0.1.0
