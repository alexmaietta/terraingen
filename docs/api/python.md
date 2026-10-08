# Python API

The `terraingen` package wraps the C++ library with nanobind: every public class and function
of the C++ library has a Python counterpart (a test checks this against the headers). Rasters
share their cells with NumPy without copying, long-running functions release the GIL, and the
base classes (distributions, age and degradation models, profiles, sources, layers, filters and
albedo) can be subclassed in Python. Build the extension module first (`pip install -e .`, or a
CMake build in `build/`) so the reference can be generated.

```{eval-rst}
.. automodule:: terraingen
   :no-members:
```

## Configs and the pipeline

Load, validate, generate and write terrains from JSON configs, or build their objects.

```{eval-rst}
.. autoclass:: terraingen.Config
.. autoclass:: terraingen.ConfigError
.. autoclass:: terraingen.GeneratedTerrain
.. autofunction:: terraingen.config_catalog
.. autofunction:: terraingen.generate_terrain
.. autofunction:: terraingen.write_outputs
.. autofunction:: terraingen.build_terrain
.. autofunction:: terraingen.build_albedo
```

## Terrains and evaluation

Compose a source, layers and filters, and evaluate them over grids, tiles or points.

```{eval-rst}
.. autoclass:: terraingen.Terrain
.. autoclass:: terraingen.EvaluatedTerrain
.. autofunction:: terraingen.evaluate
.. autofunction:: terraingen.evaluate_at
.. autofunction:: terraingen.filter_margin
.. autoclass:: terraingen.RasterGrid
.. autoclass:: terraingen.LayerContext
```

## Surfaces and cells

Planes and spheres, bodies, and the cells that tile them.

```{eval-rst}
.. autoclass:: terraingen.Surface
.. autoclass:: terraingen.Body
.. autofunction:: terraingen.body_sphere
.. autofunction:: terraingen.mean_radius
.. autofunction:: terraingen.body_named
.. autoclass:: terraingen.SurfaceCells
.. autoclass:: terraingen.PlanarCells
.. autoclass:: terraingen.CubeSphereCells
.. autoclass:: terraingen.CellIndex
```

## Sources

Where heights come from.

```{eval-rst}
.. autoclass:: terraingen.Source
.. autoclass:: terraingen.FlatSource
.. autoclass:: terraingen.GeoTiffSource
.. autoclass:: terraingen.SourceChain
.. autoclass:: terraingen.ResampleMethod
```

## Synthetic terrain and noise

Layered fractal noise and the kernels it is made of.

```{eval-rst}
.. autoclass:: terraingen.NoiseRelief
.. autoclass:: terraingen.SyntheticTerrainParams
.. autoclass:: terraingen.NoiseLayer
.. autoclass:: terraingen.FractalParams
.. autoclass:: terraingen.NoiseBasis
.. autoclass:: terraingen.FractalType
.. autoclass:: terraingen.LayerBlend
.. autodata:: terraingen.MAX_NOISE_LAYERS
.. autofunction:: terraingen.generate_synthetic_terrain
.. autofunction:: terraingen.fill_synthetic_terrain
.. autofunction:: terraingen.evaluate_noise_layers
.. autofunction:: terraingen.evaluate_noise_layers3
.. autofunction:: terraingen.evaluate_noise_layer
.. autofunction:: terraingen.domain_warp2
.. autofunction:: terraingen.gain_from_hurst
.. autofunction:: terraingen.fractal2
.. autofunction:: terraingen.fractal3
.. autofunction:: terraingen.basis_noise2
.. autofunction:: terraingen.basis_noise3
.. autofunction:: terraingen.has_basis_noise3
.. autofunction:: terraingen.perlin_noise2
.. autofunction:: terraingen.perlin_noise3
.. autofunction:: terraingen.simplex_noise2
.. autofunction:: terraingen.value_noise2
.. autofunction:: terraingen.value_noise3
.. autofunction:: terraingen.worley2
```

## Layers

Relief added to a terrain, and the base class for your own.

```{eval-rst}
.. autoclass:: terraingen.Layer
.. autoclass:: terraingen.FunctionLayer
.. autoclass:: terraingen.ScaleLayer
.. autoclass:: terraingen.ClampLayer
.. autoclass:: terraingen.RegionLayer
.. autoclass:: terraingen.RegionShape
.. autoclass:: terraingen.BlendShape
.. autoclass:: terraingen.ResolutionFade
.. autoclass:: terraingen.RoughnessLayer
.. autoclass:: terraingen.RoughnessOptions
.. autofunction:: terraingen.perlin_structure_function
.. autofunction:: terraingen.perlin3_structure_function
.. autoclass:: terraingen.BedformLayer
.. autoclass:: terraingen.BedformOptions
.. autofunction:: terraingen.mars_large_ripples
.. autofunction:: terraingen.mars_transverse_aeolian_ridges
.. autoclass:: terraingen.ExposureField
.. autoclass:: terraingen.ExposureParams
```

## Crater size-frequency distributions

Crater and rock densities, production functions and chronologies.

```{eval-rst}
.. autoclass:: terraingen.SizeFrequencyDistribution
.. autoclass:: terraingen.SfdKind
.. autoclass:: terraingen.PowerLawSfd
.. autoclass:: terraingen.PowerLawSegment
.. autoclass:: terraingen.PiecewisePowerLawSfd
.. autoclass:: terraingen.TabulatedSfd
.. autoclass:: terraingen.LogPolynomialSfd
.. autoclass:: terraingen.SummedSfd
.. autoclass:: terraingen.MinimumSfd
.. autoclass:: terraingen.SplicedSfd
.. autoclass:: terraingen.GolombekRappSfd
.. autoclass:: terraingen.ProductionSurface
.. autofunction:: terraingen.power_law_coefficient_from_kilometres
.. autofunction:: terraingen.trask_equilibrium
.. autofunction:: terraingen.hartmann_equilibrium
.. autoclass:: terraingen.MooreTerrain
.. autofunction:: terraingen.moore_distribution
.. autofunction:: terraingen.viper_specification
.. autofunction:: terraingen.neukum_production_function
.. autofunction:: terraingen.extended_neukum_production_function
.. autofunction:: terraingen.neukum_lunar_chronology
.. autofunction:: terraingen.neukum_lunar_chronology_rate
.. autofunction:: terraingen.ivanov_mars_production_function
.. autofunction:: terraingen.ivanov_mars_chronology
.. autofunction:: terraingen.ivanov_mars_chronology_rate
.. autofunction:: terraingen.exponential_integral
.. autoclass:: terraingen.SurfaceModel
.. autofunction:: terraingen.lunar_surface
.. autofunction:: terraingen.mars_surface
```

## Crater ages, degradation and profiles

How old craters are and how their shapes change.

```{eval-rst}
.. autoclass:: terraingen.CraterAgeModel
.. autoclass:: terraingen.FreshAges
.. autoclass:: terraingen.UniformAges
.. autoclass:: terraingen.SteadyStateAges
.. autoclass:: terraingen.DegradationModel
.. autoclass:: terraingen.NoDegradation
.. autoclass:: terraingen.FreshnessDegradation
.. autoclass:: terraingen.DiffusionDegradation
.. autoclass:: terraingen.PowerLawDiffusivity
.. autofunction:: terraingen.fassett_thomson_2014_diffusivity
.. autofunction:: terraingen.fassett_2022_hartmann_diffusivity
.. autofunction:: terraingen.fassett_2022_trask_diffusivity
.. autofunction:: terraingen.fassett_thomson_2014_diffusion_age
.. autoclass:: terraingen.CraterMorphometry
.. autoclass:: terraingen.CraterState
.. autofunction:: terraingen.lunar_fresh_crater_morphometry
.. autofunction:: terraingen.lunar_fresh_crater_depth_ratio
.. autofunction:: terraingen.mars_fresh_crater_morphometry
.. autoclass:: terraingen.CraterProfile
.. autoclass:: terraingen.FassettThomsonProfile
.. autoclass:: terraingen.FassettThomsonRim
.. autoclass:: terraingen.MartinParkesDunstanProfile
.. autoclass:: terraingen.ComplexCraterProfile
.. autoclass:: terraingen.ComplexCraterShape
.. autoclass:: terraingen.KilometrePowerLaw
.. autofunction:: terraingen.lunar_complex_crater_shape
.. autofunction:: terraingen.mars_complex_crater_shape
.. autoclass:: terraingen.DiffusedProfile
```

## Craters

Crater layers, crater lists and the functions that generate, place and apply them.

```{eval-rst}
.. autoclass:: terraingen.CraterModels
.. autoclass:: terraingen.CraterLayer
.. autoclass:: terraingen.CraterLayerOptions
.. autoclass:: terraingen.CraterGenerationOptions
.. autoclass:: terraingen.CraterSpacing
.. autoclass:: terraingen.CraterApplyOptions
.. autoclass:: terraingen.CraterOrder
.. autoclass:: terraingen.CraterDatum
.. autoclass:: terraingen.CraterBlend
.. autoclass:: terraingen.CraterIrregularity
.. autoclass:: terraingen.RimRoughnessModel
.. autoclass:: terraingen.ValueDistribution
.. autoclass:: terraingen.ValueDistributionKind
.. autoclass:: terraingen.CraterOutline
.. autoclass:: terraingen.Crater
.. autoclass:: terraingen.CraterAttributes
.. autoclass:: terraingen.CraterList
.. autoclass:: terraingen.CraterBand
.. autofunction:: terraingen.crater_bands
.. autofunction:: terraingen.generate_craters
.. autofunction:: terraingen.min_diameter_for_count
.. autofunction:: terraingen.manual_crater_id
.. autofunction:: terraingen.apply_craters
.. autofunction:: terraingen.crater_apply_order
.. autofunction:: terraingen.crater_blend_weight
.. autofunction:: terraingen.resolution_weight
.. autofunction:: terraingen.compute_crater_attributes
.. autofunction:: terraingen.fit_crater_datum
.. autofunction:: terraingen.craters_conflict
.. autofunction:: terraingen.crater_takes_precedence
.. autofunction:: terraingen.thin_by_spacing
.. autofunction:: terraingen.spacing_reach
.. autofunction:: terraingen.outline_of
.. autofunction:: terraingen.max_outline_scale
.. autoclass:: terraingen.SecondaryCraterLayer
.. autoclass:: terraingen.SecondaryCraterOptions
.. autofunction:: terraingen.singer_max_secondary_diameter
.. autofunction:: terraingen.secondary_onset_range
```

## Rocks and boulders

Rock layers, rock size models and 3D boulder shapes.

```{eval-rst}
.. autoclass:: terraingen.RockLayer
.. autoclass:: terraingen.RockLayerOptions
.. autoclass:: terraingen.RockPatches
.. autoclass:: terraingen.RockEjecta
.. autoclass:: terraingen.EjectaProfile
.. autoclass:: terraingen.EjectaAging
.. autoclass:: terraingen.RockRelief
.. autoclass:: terraingen.RockModels
.. autoclass:: terraingen.RockModelCurve
.. autofunction:: terraingen.sp8023_intercrater_blocks
.. autofunction:: terraingen.sp8023_crater_blocks
.. autoclass:: terraingen.SynthterrainRocks
.. autofunction:: terraingen.synthterrain_rock_distribution
.. autofunction:: terraingen.rock_candidates
.. autofunction:: terraingen.boulder_throw_distance
.. autoclass:: terraingen.Rock
.. autofunction:: terraingen.add_rock
.. autoclass:: terraingen.BoulderStyle
.. autoclass:: terraingen.ValueRange
.. autoclass:: terraingen.BaseSolid
.. autofunction:: terraingen.default_boulder_styles
.. autoclass:: terraingen.BoulderShapeOptions
.. autoclass:: terraingen.BoulderShape
.. autofunction:: terraingen.generate_boulder_shape
.. autofunction:: terraingen.boulder_shape_seed
.. autoclass:: terraingen.BoulderModel
.. autoclass:: terraingen.BoulderPlacement
.. autofunction:: terraingen.place_boulder
.. autofunction:: terraingen.boulder_mesh
.. autofunction:: terraingen.placement_at_site
.. autofunction:: terraingen.top_surface
```

## Filters

Transforms of the heights after every layer.

```{eval-rst}
.. autoclass:: terraingen.Filter
.. autoclass:: terraingen.RimSmoothingFilter
.. autoclass:: terraingen.RimSmoothing
.. autofunction:: terraingen.smooth_crater_rims
```

## Albedo

Surface albedo: synthetic colour, maps from orbit, and bright crater material.

```{eval-rst}
.. autoclass:: terraingen.Albedo
.. autoclass:: terraingen.AlbedoSource
.. autoclass:: terraingen.AlbedoLayer
.. autoclass:: terraingen.AlbedoContext
.. autoclass:: terraingen.SyntheticAlbedo
.. autoclass:: terraingen.RasterAlbedo
.. autoclass:: terraingen.RasterAlbedoOptions
.. autoclass:: terraingen.AlbedoDetail
.. autoclass:: terraingen.CraterAlbedo
.. autoclass:: terraingen.CraterAlbedoOptions
.. autoclass:: terraingen.CraterRays
.. autoclass:: terraingen.ObliqueImpacts
.. autoclass:: terraingen.RayRetention
.. autoclass:: terraingen.CraterImpact
.. autofunction:: terraingen.evaluate_albedo
.. autofunction:: terraingen.evaluate_albedo_at
.. autoclass:: terraingen.OpticalMaturityOrigin
.. autofunction:: terraingen.optical_maturity
.. autofunction:: terraingen.reflectance_at_maturity
.. autofunction:: terraingen.hsv_to_rgb
```

## Tiles and whole bodies

Quadtree tiles, tile caches and meshes of whole bodies.

```{eval-rst}
.. autoclass:: terraingen.TileScheme
.. autoclass:: terraingen.TileKey
.. autoclass:: terraingen.TileCache
.. autofunction:: terraingen.evaluate_tile
.. autofunction:: terraingen.evaluate_tiles
.. autofunction:: terraingen.body_mesh
```

## Meshes

Triangle meshes, transforms and glTF scenes.

```{eval-rst}
.. autoclass:: terraingen.TriangleMesh
.. autoclass:: terraingen.RigidTransform
.. autoclass:: terraingen.SceneNode
.. autofunction:: terraingen.write_glb_scene
.. autofunction:: terraingen.base_solid_mesh
.. autofunction:: terraingen.icosphere
```

## Rasters and GeoTIFF

Rasters shared with NumPy, GeoTIFF reading and writing, and resampling.

```{eval-rst}
.. autoclass:: terraingen.Raster
.. autoclass:: terraingen.GeoTransform
.. autoclass:: terraingen.RasterStats
.. autofunction:: terraingen.compute_stats
.. autofunction:: terraingen.read_geotiff
.. autofunction:: terraingen.read_geotiff_info
.. autofunction:: terraingen.write_geotiff
.. autoclass:: terraingen.GeoTiffInfo
.. autoclass:: terraingen.GeoTiffWriteOptions
.. autoclass:: terraingen.GeoKeys
.. autofunction:: terraingen.geographic_sphere_keys
.. autoclass:: terraingen.BandScaling
.. autoclass:: terraingen.TiffCompression
.. autoclass:: terraingen.TiffSampleFormat
.. autoclass:: terraingen.GeoTiffError
.. autofunction:: terraingen.gaussian_blur
.. autofunction:: terraingen.resample
.. autofunction:: terraingen.resample_to
.. autofunction:: terraingen.sample_raster
```

## Hashing and random numbers

Counter-based random numbers, as the generators use.

```{eval-rst}
.. autofunction:: terraingen.pcg_hash
.. autofunction:: terraingen.pcg3d_hash
.. autofunction:: terraingen.philox4x32
.. autofunction:: terraingen.hash_to_unit_float
.. autoclass:: terraingen.RandomStream
.. autoclass:: terraingen.TabulatedDistribution
.. autofunction:: terraingen.sample_exponential
.. autofunction:: terraingen.sample_normal
.. autofunction:: terraingen.sample_poisson
.. autofunction:: terraingen.sample_truncated_power_law
```

## Backends

Choosing where evaluation runs.

```{eval-rst}
.. autoclass:: terraingen.Backend
.. autoclass:: terraingen.BackendStatus
.. autofunction:: terraingen.backends
.. autofunction:: terraingen.backend_named
.. autofunction:: terraingen.default_backend
.. autofunction:: terraingen.set_default_backend
.. autofunction:: terraingen.resolve_backend
```

## Validation

Check settings without building anything; constructors raise the same errors.

```{eval-rst}
.. autofunction:: terraingen.validate_albedo_context
.. autofunction:: terraingen.validate_albedo_detail
.. autofunction:: terraingen.validate_bedform_options
.. autofunction:: terraingen.validate_boulder_shape_options
.. autofunction:: terraingen.validate_complex_crater_shape
.. autofunction:: terraingen.validate_crater_albedo
.. autofunction:: terraingen.validate_crater_apply_options
.. autofunction:: terraingen.validate_crater_irregularity
.. autofunction:: terraingen.validate_crater_rays
.. autofunction:: terraingen.validate_crater_spacing
.. autofunction:: terraingen.validate_exposure_params
.. autofunction:: terraingen.validate_fractal_params
.. autofunction:: terraingen.validate_noise_layer
.. autofunction:: terraingen.validate_oblique_impacts
.. autofunction:: terraingen.validate_power_law_diffusivity
.. autofunction:: terraingen.validate_ray_retention
.. autofunction:: terraingen.validate_region_shape
.. autofunction:: terraingen.validate_resolution_fade
.. autofunction:: terraingen.validate_rim_smoothing
.. autofunction:: terraingen.validate_rock_layer_options
.. autofunction:: terraingen.validate_roughness_options
.. autofunction:: terraingen.validate_secondary_crater_options
.. autofunction:: terraingen.validate_synthetic_terrain
.. autofunction:: terraingen.validate_value_distribution
```

## Logging and version

Log records and build information.

```{eval-rst}
.. autofunction:: terraingen.set_log_level
.. autofunction:: terraingen.get_log_level
.. autofunction:: terraingen.set_log_sink
.. autofunction:: terraingen.use_python_logging
.. autofunction:: terraingen.version
.. autofunction:: terraingen.api_version
.. autofunction:: terraingen.build_info
.. autodata:: terraingen.__version__
```
