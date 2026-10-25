# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Python `ValueDistribution.sample(u1, u2)`, `draw(stream, count=None)` (a NumPy array of `count` draws when given), `lowest()` and `highest()`, as in C++.
- Python bindings for `FitDeviogram` (`fit_deviogram`, `DeviogramFit`), `ValidateConfig` (`validate_config`, `ConfigSummary`; `TerrainGenConfig.validate` returns the summary), `WriteCraterList`, `WriteBoulderList` and `WriteBoulderModels` (`write_crater_list`, `write_boulder_list`, `write_boulder_models`, `ListFormat`), `FragmentBoulderShape` (`fragment_boulder_shape`, `BoulderFragment`, `BoulderCracks`), `SecondaryDensity` and `SecondaryRayField`, `DescribeCraters` (`describe_craters`, `CraterShapeModels`, `CraterModels.shape`, `CraterFeatures.attributes`) and `BlastZoneMeanShare` (`blast_zone_mean_share`).
- `RasterGrid::Centered(center, size, pixelSize)` and `RasterGrid::Covering(box, pixelSize)`: the north-up grid covering a box, snapped outward to whole cells as configuration outputs snap their extents (`SnappedCells`, `kGridSnapTolerance`).
- `ManualCrater(x, y, diameter, depth, rimHeight)` (`manual_crater`) and a `CraterList` built from a list of craters, `CraterList{a, b, c}`.
- `BoulderList` (Python `BoulderList`): boulders stored one column per field, as `CraterList` stores craters, built from `Boulder` records (`Append`, `BoulderList{a, b, c}`) and read back with `At`.
- `BuildGenerator(config, source)` (`build_generator(config, source=...)`): a configuration's layers built on a source the caller gives, such as an embedder's DEM, which they read as measured data (`source_resolves_px`, crater datums); the configuration gives no source of its own.
- Point queries keep fitted crater datums between calls, so craters with a datum are fitted once and later queries near them run as fast as craters without one (`BM_HeightAtDatum` about 1k to 340k point queries per second). `HeightsAt` and `HeightAt` share a cache kept per generator, matched by its surface, source and layers and dropped once one of them is freed; pass an `EvaluationCache` (`cache=` in Python) to manage one yourself.

### Changed

- Every configuration key in meters ends in `_m`, as keys in degrees, pixels and Gyr already end in `_deg`, `_px` and `_gyr`: 61 keys gained it, among them `baseline_m`, `bed_thickness_m`, `core_radius_m`, `cover_thickness_m`, `crack_depth_m`, `crack_width_m`, `depth_m`, `diameter_m`, `dust_patch_m`, `edge_rounding_m`, `exposure_spacing_m`, `field_size_m`, `floor_patch_m`, `height_m`, `large_diameter_m`, `mature_depth_m`, `max_crater_diameter_m`, `max_diameter_m`, `max_lobe_length_m`, `max_lobe_width_m`, `max_range_m`, `max_scarp_length_m`, `max_scarp_width_m`, `max_step_m`, `max_thickness_m`, `max_wavelength_m`, `mid_diameter_m`, `min_crater_diameter_m`, `min_diameter_m`, `min_lobe_length_m`, `min_lobe_width_m`, `min_range_m`, `min_scarp_length_m`, `min_scarp_width_m`, `min_wavelength_m`, `min_width_m`, `octave_rms_m`, `pixel_size_m`, `plate_size_m`, `radii_m`, `radius_m`, `reference_diameter_m`, `rim_height_m`, `small_diameter_m`, `start_m`, `thickness_m`, `warp_amplitude_m`, `warp_wavelength_m`, `wavelength_m`, `width_m`, and the terrain noise `amplitude_m`, `spacing_m`, `feather_m`, `reach_m`, `offset_m` and `scale_m` where those are meters; heightmap `tile_size` is `tile_size_px`. Albedo noise layers keep `amplitude`, which is a relative contrast or degrees there, and conversion factors keep `diameter_unit` and `meters_per_unit` (unit m/unit). Python names are unchanged, as they drop `_m`.
- Every layer gives its resolution fade as craters and boulders do, `"fade_px": [start, end]` in output pixels and `"fade_shape"`: roughness, dune, ripple, grainflow, pavement, sand shadow, bedform and spectral continuation layers and albedo detail, crater albedo and splotches took a `"fade"` object of `start_samples`, `end_samples` and `shape`. In Python every fade is one `ResolutionFade`, `options.fade`, as in C++; `fade_px` and `fade_shape` on `CraterApplyOptions` and `BoulderLayerOptions` are gone.
- `SandFillLayer::ThicknessAt(x, y, spacing, ground)` takes the ground last, as `SandShadowLayer::ThicknessAt` and the other point queries do (it took it first).
- Worn craters round out and lower their ejecta as they wear: a crater keeps its rough rim wobble in proportion to its current over its fresh rim height, each harmonic of order k by that share to the power k / 2, so worn rims lose their short wiggles first rather than showing them as lumps along a rounded rim (`KernelWornOutlineShare`; as diffusion already blurred them); and Martin, Parkes and Dunstan ejecta thickness scales with the rim height, so worn craters no longer dip below the ground in a moat outside the rim.
- `FassettThomsonProfile` exteriors are tapered as Martin, Parkes and Dunstan's are, by 1 - u^2 from where the exterior starts to 1.5 radii, so their slope also falls to 0 at the reach rather than the crater being cut off while still sloping (`FassettThomsonExterior`); every Fassett and Thomson crater's exterior changes, its rim join does not.
- `MartinParkesDunstanProfile` craters join the ground without slope breaks: the ejecta outside the rim is tapered by 1 - x^2 (x the distance beyond the rim in radii), so its slope falls to 0 at the 2 R reach rather than the profile being cut off while still sloping, and a worn crater's rounded rim is a cubic matching the ejecta's height and slope where they meet, which kinked before (`MartinParkesDunstanExterior`). In hillshades the rings at the rim join and at 2 R are gone. Every Martin, Parkes and Dunstan crater's exterior changes; its crest, rim height and interior do not.
- Every height and albedo layer takes a `TerrainBase{surface, source}` (Python `TerrainBase`, `base=`) as its last argument in place of separate surface and source arguments, so one base built for a terrain is passed to each of its layers; a `Surface` or a source alone converts to one. `CraterLayerOptions::surface` is gone (the crater layer takes its surface from the base), `NoiseRelief` and `SyntheticAlbedo` measure noise from the base source's reference point in place of an `origin` argument, and `NoiseRelief` takes `NoiseReliefOptions{layers, sourceResolvesPixels}` (`Options()` replaces `Layers()` and `SourceResolvesPixels()`). `SecondaryCraterLayer` rejects a base on another surface than its primaries'.
- Layers on `FormulaLayer` (roughness, bedforms, dunes, strata, pavement, sand fill, function layers) report their surface (`Layer::LayerSurface`, Python `layer_surface`), so `ValidateGenerator` rejects them on another surface than the terrain's; scale and clamp layers work on any.
- Python `BoulderLayerOptions` nests its parts as the C++ struct does: `sizes` (`BoulderSizes`: `coverage`, `q_constant`, `q_coverage_coefficient`, `distribution`, `min_diameter`, `max_diameter`), `bedrock` (`BoulderBedrock`: `exposure`, `abundance`, `mean_size`), `pose` (`BoulderPose`) and `rendering` (`BoulderRendering`: `relief`, `model_threshold`, `relief_range`, `shapes`, `shape_prototypes`), which were flat properties named for the configuration keys (`coverage`, `qa`, `qb`, `bedrock_abundance`, `min_burial`, `relief`, ...); `cache_boulders` is bound. Configurations keep their flat keys.
- `BoulderModelThreshold::minSamples` and `maxSamples` are `minPixels` and `maxPixels`, as Python names them (`min_pixels`, `max_pixels`) and configurations give them (`min_px`, `max_px`).
- `DuneOptions::ripples` and `grainflows` are optional, unset for none (Python `None`); they were on with a 0 `wavelength` or `cover` meaning none. `DuneRipples` defaults to the 2.1 m, 0.2 m large ripples of Mars's Bagnold dunes and requires a positive wavelength, and `DuneGrainflows` defaults to a `cover` of 1. Configurations give ripples and grainflows as before.
- `Crater::depth` and `rimHeight` are `std::optional<double>`, unset where the models give them (they were NaN); Python `Crater.depth` and `rim_height` are `None` when unset. `CraterList` columns keep NaN for unset values.
- A crater layer's `ages` (`CraterLayerSpec::ages`) is optional: unset gives steady-state ages on the production surface and equilibrium its density is the minimum of (`config::LayerAges`, `config::MatchingAges`), so a density capped by Trask no longer needs its ages repeated. The default ages were always Hartmann, which made any Trask-capped density without ages fail validation.
- Size-frequency constructors take the data, then the kind, then the range: `LogPolynomialSfd(coefficients, kind, range, ...)` (was range before kind; Python `LogPolynomialSfd(coefficients, kind, min_diameter, max_diameter)`). `SplicedSfd(pieces, maxDiameter, tolerance)` and `PiecewisePowerLawSfd(segments, kind, maxDiameter, joins)` are valid from their first piece's start, so they take only the largest diameter (they took a `DiameterRange` whose minimum had to repeat it); `SfdJoins{allowDiscontinuities, tolerance}` (Python `SfdJoins`, `joins=`) replaces the bare `bool` and tolerance.
- `SyntheticAlbedoOptions::hue` and `hueNoise` are `hueDegrees` and `hueNoiseDegrees`, as `SyntheticAlbedoSpec`'s are (Python `hue_degrees` and `hue_noise_degrees`, and `hsv_to_rgb(hue_degrees, ...)`); configuration keys keep `hue_deg` and `hue_noise_deg`.
- Rough crater rims fade toward a round floor: the profile follows an outline's rough wobble less the nearer it is read to the center, none there and all of it at the rim, each harmonic of order k by the share to the power k / 2 so short wiggles stay near the rim (`KernelOutlineScaleAt`, `KernelOutlineShare`). At high roughness, rim kinks no longer run as straight radial seams through the wall to the center; every irregular crater's inner wall changes, its rim and outline do not.
- A crater layer given no profile, under a degradation that requires none, shapes its craters with Martin, Parkes and Dunstan's profile (`MatchedProfile`), as `DefaultCraterModels` and configurations do; it threw before.
- Python option fields are named for their C++ members: a `_m` unit suffix is dropped (`min_diameter`, `ground_spacing`), and `_deg` and `_px` are spelled `_degrees` and `_pixels` (`yaw_degrees`, `max_slope_degrees`, `source_resolves_pixels`), as the C++ fields are; `validate_source_resolves_px` is `validate_source_resolves_pixels`. Configuration keys keep their suffixes, and placed boulders, outlines and other records keep their `_deg` properties.
- Public angles are in degrees, named for it, matching the configuration's `*_deg` keys: `CraterIrregularity::headingDegrees` and `headingSpreadDegrees`, `CraterSlopeResponse::fullSlopeDegrees`, `ObliqueImpacts::headingDegrees`, `BoulderPose::maxLeanDegrees`, `maxTiltDegrees` and `maxSlopeDegrees`, `BoulderVentifacts::windAzimuthDegrees`, `minAngleDegrees`, `maxAngleDegrees` and `secondWindAzimuthDegrees`, `DuneOptions::windAzimuthDegrees`, `azimuthSpreadDegrees`, `slipFaceAngleDegrees`, `stossSlopeDegrees` and `flankSlopeDegrees`, `PavementOptions::maxTiltDegrees`, `StrataOptions::dipDegrees` and `dipAzimuthDegrees`, `SandShadowOptions::windAzimuthDegrees` and `flankAngleDegrees`, and `BedformOptions::windAzimuthDegrees` and `azimuthSpreadDegrees` (all were radians). Placed boulders and other outputs keep radians.
- Names that clashed with others: `BoulderClusters::spacing` is `centerSpacing` (key `center_spacing_m`), `DuneOptions::spacing` is `latticeSpacing` (key `lattice_spacing_m`, was `spacing`), `BoulderRendering::models` is `modelThreshold` (key `model_threshold`) and `BoulderReliefRange::models` is `includeModels` (key `include_models`).
- `SyntheticSource` takes its `pixelSize` in meters on a body too, as `FlatSource` does (it took the surface's units, degrees on a body).
- `ValidateGenerator`, and so every evaluation, rejects a layer built for another surface than the terrain's (`Layer::LayerSurface`) and a layer whose inputs, such as a boulder layer's crater layers or a sand shadow layer's boulder layer, are missing from the terrain's layers or come after it (`Layer::Dependencies`).
- `LoadConfig` remembers the configuration's folder in `TerrainGenConfig::directory`, which `ValidateConfig`, `BuildGenerator`, `BuildAlbedo` and `GenerateFromConfig` resolve relative paths against when given no folder (`ConfigDirectory`).
- `WriteGeoTiff(path, raster, generator)` writes in the terrain's projection (`TerrainGeoKeys`), and `WriteMesh(path, mesh)` writes a `TriangleMesh` alone.
- `ManualBoulder` is built by name: `ManualBoulder(x, y, diameter)` with chainable setters (`.Height(1.1).Burial(0.2).YawDegrees(30.0).Rest(BoulderRest::Side)`), every field it leaves to be drawn a `std::optional` instead of NaN (and `id` instead of 0), its angles in degrees (`yawDegrees`, `leanDegrees`, `tiltDegrees`, `tiltAzimuthDegrees`, as the configuration's `*_deg` keys), and a new `model` that makes it a 3D model, or keeps it relief, whatever the layer's size threshold.
- `ManualCrater(x, y, diameter)` takes no depth or rim height, whose position clashed with `Crater{x, y, diameter, ageGyr}`; set them, and the age, by name with `Crater`'s chainable `.AgeGyr()`, `.Depth()` and `.RimHeight()` (Python `manual_crater(x, y, diameter)`, then its attributes).
- Named construction: `ValueDistribution::Constant`, `Uniform`, `Normal`, `LogNormal`, `Piecewise` and `.Clamped(min, max)` (Python `ValueDistribution.normal(...)` and so on), `EvaluationOptions{}.Spacing(s).Threads(n).On(backend)`, `NoiseLayer::Fbm(seed, wavelength, amplitude)`, and `NoiseRelief(NoiseLayer)` for one layer.
- Off and unset are `std::optional` where a sentinel stood for them: `BoulderLayerOptions::fragmentation` is optional (none break unset; was off until `splitWear` dropped below 1, `BoulderFragmentation::Enabled` is gone, and `splitWear` defaults to 0.5); `BoulderEjecta::minCraterDiameter` is optional (was 0 for none) and `MinCraterDiameter()` is `ThrowingDiameter()`, unset when no crater throws blocks; `CraterGenerationOptions::maxCraters` and the configuration's `max_craters` are optional (was 0 for no cap); `ComplexCraterShape::peakFitMaxDiameter` is optional (was 0 for every size).
- Crater shaping seeds (`irregularity`, `noise`, `texture` and `slope`) are optional and, unset, follow the crater layer's `seed` (`SeededCraterApply`), so a new layer seed reshapes the outlines, noise, texture and floor offsets too; they were 0 whatever the layer's seed. Craters with these features on change unless their seeds are set.
- `CraterSlopeResponse::enabled` is `asymmetric` (configuration key `asymmetric`, Python `asymmetric`), as it turns on only the asymmetry: the wear `criticalGradient` gives applies either way.
- Settings that would be lost are rejected instead: `BoulderLayer::Manual` (Python `BoulderLayer.manual`) throws for options whose `manual` is not empty, which it replaced; `BuildBoulderOptions` throws for a `BoulderLayerSpec` whose `boulders.sizes.distribution` or `boulders.ejecta.distribution` is not the default, which `sizes` and `ejecta_sizes` replaced; and a raster source spec's `read.upsampler`, which `upsampling` replaced, must stay unset.
- Crater ages and surfaces are given once: a crater layer whose `CraterModels::ages` is null draws the ages its degradation wears by (`DegradationModel::AgeModel`, `FreshnessDegradation`'s), `EquilibriumSurface{production, equilibrium, ageGyr, chronology}` (Python `EquilibriumSurface`) gives a surface's `Density()` and steady-state `Ages()` from one set of models, `EquilibriumDiffusionAges(profile, fresh, steadyAges)` and `MatchedDiffusionDegradation` match diffusion to steady-state ages in one call, and a configuration whose steady-state ages name another production function, chronology, age or equilibrium than its density's production surface and equilibrium is rejected at `ages`.
- `SecondaryCraterLayer(primaries, options)` (Python `SecondaryCraterLayer(primaries, options)`) shapes secondaries as the primaries are, and a secondary layer on primaries following a density map throws unless its options are among the primaries' `CraterLayerOptions::secondaries`, which the map counts.
- Crater layers built in code default as a configuration's do: `DefaultCraterModels()` (`default_crater_models`) gives the configuration's default density, ages, profile and degradation, `CraterLayerOptions` generates from 2.5 m (was 4 m) and shapes with `DefaultCraterApply()` (irregular outlines on, as configurations always had; it moves from `config.h` to `shaping/apply_options.h`), and `CraterLayer`'s models default to `DefaultCraterModels()`. The density and ages may be null for a layer that generates no craters.
- Roughness, bedform, dune and boulder options built in code default as a configuration's do: roughness octaves up to 16 m (was 4 m), bedforms 4 m apart, 0.4 m high over 90% of the ground (were 1 m, 0.1 m and none), barchans 50 m wide from 15 m in 129 m cells (were 400 m, 200 m and 800 m), and boulder sizes between craters and around them from the nominal SP-8023 intercrater blocks up to their largest, about 5.86 m (`DefaultBoulderSizes`; were Golombek-Rapp sizes up to 4 m). `DefaultBoulders`, `DefaultRoughness`, `DefaultBedforms` and `DefaultDunes` are gone; `PlaceBoulderModel` settles by default, as the layer does.
- The Golombek-Rapp terms `BoulderSizes::coverage`, `qConstant`, `qCoverageCoefficient` and `BoulderEjecta::coverage` are optional (`Coverage()`, `QConstant()`, `QCoverageCoefficient()` give them or their defaults), and `ValidateBoulderLayerOptions` rejects one set beside the size distribution that leaves it unused; Golombek-Rapp sizes need `sizes.distribution` (Python `distribution`) set to null, which Python now accepts.
- `CraterLayer::Manual(craters, models, apply, surface)` (Python `CraterLayer.manual(craters, models=None, apply=None)`) takes its craters first, with the models defaulting to `DefaultCraterModels()` and the shaping to `DefaultCraterApply()`.
- Crater layers take their profile from a degradation model that requires one (`DegradationModel::RequiredProfile`, `MatchedProfile`): `DiffusionDegradation` gives its `DiffusedProfile`, so `CraterModels::profile` (Python `profile=`) may be left null, and a different profile is rejected. The diffusion terrains of the `craters_degradation` and `ages_models` examples were shaped with the plain Fassett-Thomson profile and now are diffused.
- `BuildAlbedo` throws a `ConfigError` naming the layer when the generator's layers are not the ones the configuration builds, as after changing a built generator, instead of reading past them.
- Albedo noise layers in a configuration must give their `amplitude`; unset it was 100, which saturated the albedo.
- `BoulderFeatures::boulders`, `BoulderLayer::Boulders` and `WriteBoulderList` hold or take a `BoulderList` instead of `std::vector<Boulder>`; Python `BoulderLayer.boulders(region)` returns a `BoulderList` like C++ (its `.table` gives the NumPy columns it returned before).
- `NoiseRelief` takes its noise layers as `std::vector<NoiseLayer>` and returns them from `Layers()`, as the Python binding always did (it took `SyntheticTerrainParams` and had `Params()`).
- Python `RasterGrid.centered(center, size, pixel_size)` takes the C++ arguments, a center and a size in world units, instead of cell counts (`centered(width, height, pixel_size, center)`).
- `config::SnappedGrid`, `config::SnappedCells` and `config::kExtentSnapTolerance` give way to `RasterGrid::Covering`, `SnappedCells` and `kGridSnapTolerance` in `raster/grid.h`.

- Point queries on a body find the cells around them directly from the neighbors of the cell holding the point, instead of sampling the region: about 1.8x faster on a sphere, within 1.2x of a plane.
- Generated cells keep their craters indexed by y, so a point query visits only the craters in its rows instead of copying every crater of the cells around it: point queries about 2x faster on a plane and 1.5x on a sphere.

- `terraingen schema` prints the configuration schema (`ConfigSchema`); the catalog of types with their defaults stays available as Python `config_catalog()`.
- Every number in a configuration must be finite, and such errors name the number itself, such as `layers[1].manual[0].x`.
- `terraingen/config/report.h` is now `terraingen/pipeline/report.h`; `WriteCraterList`, `WriteBoulderList`, `WriteBoulderModels` and `ListFormat` move to `terraingen/pipeline/feature_lists.h`, and `PlacedCraters` and `PlacedBoulders` to `terraingen/pipeline/generator.h` (`pipeline/features.h` is gone).
- C++ renames: `NoiseLayerConfig` to `NoiseStackLayerSpec`, `SyntheticAlbedoSpec::Params` to `Options`, `estimateFromBase` to `estimateFromSource` (the config key stays `estimate_from_base`); `CheckConfigRanges` and `ValidateAlbedo` move into `terraingen::config`.
- `TerrainGenConfig` compares equal when its JSON does; the nested specs no longer have `operator==`.
- Headers moved: `sources/synthetic_terrain.h` into `sources/synthetic_source.h`; `layers/bisection.h` to `util/bisection.h`; `Plane` and `PlaneFit` to `util/plane_fit.h`; the power spectrum fit (`EstimatePowerLawSpectrum`, `PowerLawSpectrum`) to `raster/spectrum.h`; the Perlin structure functions to `noise/structure_function.h`; `NoiseLayersAt` to `noise/surface_noise.h`; the crater albedo options to `albedo/crater_albedo_options.h`; `CutAtPlane` (was boulder-internal) to `mesh/mesh.h`; crater `ages/freshness.h` to `ages/freshness_ages.h`, `degradation/freshness.h` to `degradation/freshness_degradation.h` and `shaping/explicit_state.h` to `shaping/resolved_state.h`.
- Crater profiles (Fassett and Thomson, Martin-Parkes-Dunstan, complex, diffused) evaluate the same float formulas on the CPU as on the GPU, so their heights match the kernel; `FassettThomsonProfile::Rim` and `DiffusedProfile::Cells` give way to `Kernel()`.
- Crater outlines (`CraterOutline::Scale`, `MarkShift`, `CraterProfileDistance`), diffused morphometry and the kilometer power laws (`KilometerPowerLaw::At`) evaluate the crater kernel's float formulas on the CPU too, so no double twin remains; `KernelOutline` holds an outline in the kernel's form for many points. Outline distances, fresh complex and Martian morphometry and diffusion depths change at float rounding.
- Crater datums without a ground sampler read the raster with `SampleRaster`; bedforms place their fields on the surface cells (`MakeCells`) on planes and bodies alike; spectral continuation impulses draw from lattice streams. These change generated heights slightly.
- `ValidateNoiseLayers` (`validate_noise_layers`) checks a noise stack for synthetic sources, noise relief and synthetic albedo alike.
- A default `SyntheticTerrainParams` holds no layers and is flat, matching an empty `stack` in a configuration (it held one Perlin fBm layer); `ValidateSyntheticTerrain` accepts an empty stack, and only `NoiseRelief` still needs a layer.
- Raster albedo measures the map's roughness and Hurst exponent with the raster variogram fit, and crater zones and splotches share one overlay of changes; albedo detail, zone and splotch values change slightly, and swarm splotches, crater zones and the crater albedo grid split into shorter steps.
- Crater and boulder list outputs ending in `.json` in any case (`.JSON`) are written as JSON.

### Removed

- APIs the library itself never used, in C++ and Python alike: `GaussianBlur` (`gaussian_blur`), `Resample` and `ResampleTo` (`resample`, `resample_to`), `SampleTruncatedPowerLaw`, `SampleExponential`, `SampleNormal` and `TabulatedDistribution`, `RandomStream::NextFloat` and `NextDoubleOpenBelow`, the mesh `Volume`, `SurfaceArea` and `Transformed` (`volume`, `surface_area`, `transformed`), `Icosphere` (`icosphere`; subdivide the icosahedron base solid onto the unit sphere instead), and the `WriteGeoTiff` overload taking band pointers.
- C++ declarations only tests used: `ResampleWindow`, `SampleRasterWindow`, `SampleAtCenters`, `RequireIncreasing`, `ResolveThreads`, `StandardErrorLogSink`, `Variogram`, `LruCache::Capacity`, the `PixelWindowIn` overload taking a raster, `kSqrt2`, `kEulerGamma` and `TERRAINGEN_VERSION_STRING`; `KernelWeight`, `ParseGdalBandScaling`, `RotationQuaternion` and `GeoKeys::ShortKey` (now `ShortGeoKey`) moved to `detail`.
- `DescribeSource` and Python `TerrainGenConfig.source_grid` (use `ValidateConfig(...).source`), `LayerAt` and `LayerTimer::Callback`.
- `OpticalMaturityOrigin` (Python `OpticalMaturityOrigin`): its reflectance is now `kOpticalMaturityOriginReflectance` (`OPTICAL_MATURITY_ORIGIN_REFLECTANCE`, 0.08) and its ratio had no use left.
- `FillSyntheticTerrain` and `GenerateSyntheticTerrain` (`fill_synthetic_terrain`, `generate_synthetic_terrain`; use `SyntheticSource(params).Fill`), `OpticalMaturity`, `SyntheticSource::Elevation`, `KernelUpsampler::Method`, `GolombekRappSfd::Coverage` and `Q`, `BlastZoneAlbedo::Zones`, `DiamondSquareUpsampler::Levels`, `RasterPosts::Window` and `Placement`, `GeoTiffSource::Scaling`.
- `UnresolvedCraterLimit` (`unresolved_crater_limit`; use `SourceResolvedLimit`), `CraterBlend::Weight`, `RimSmoothing::Weight`, `EjectaThicknessShare`, `CraterLayer::Attributes` and `SecondaryCraterLayer::Attributes`, `ApplyCraterTo`, `CraterCellCache::Size` and `Evictions`, `SecondaryDensity::Diameter`.
- `AddBoulder`, `BoulderLayer::DensityAt`, `BoulderMesh`, `BandFrequencies`, `EdgeDistanceAt`, `ReliefAt`, `BedPositionAt`; `DomeHeight`, `NormalizedBoulderShape`, `TargetPsd`, `TerracedAt`, `EstimateBaseRoughness`, `RadialPowerSpectrum` and the five boulder sub-validators are internal.

### Fixed

- Boulder layers measure unresolved cover, and rock albedo shades, at spacings where the fade reaches the largest boulder: `ForEachLogSimpsonNode` ends exactly at its bounds, so a size distribution is no longer evaluated just past its largest diameter (default boulders threw at 2 m spacing).

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
| `body.metres_per_unit` (`shape_model`) | `body.meters_per_unit` |

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
| craters `apply.datum` `"centre_height"` | `"center_height"` |
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
| `KilometrePowerLaw`, `PowerLawCoefficientFromKilometres` | `KilometerPowerLaw`, `PowerLawCoefficientFromKilometers` |
| `CraterDatum::CentreHeight` | `CraterDatum::CenterHeight` |
| `CubeSphereCells::Neighbours`, `SampleAtCentres` | `CubeSphereCells::Neighbors`, `SampleAtCenters` |
| `MeshMaterial::colour`, `FeatureCell::centre`, `BoulderFragment::centre` | `MeshMaterial::color`, `FeatureCell::center`, `BoulderFragment::center` |
| `ShapeModelSurfaceSpec::metresPerUnit` | `ShapeModelSurfaceSpec::metersPerUnit` |
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
| `KilometrePowerLaw`, `power_law_coefficient_from_kilometres` | `KilometerPowerLaw`, `power_law_coefficient_from_kilometers` |
| `CraterDatum.CENTRE_HEIGHT` | `CraterDatum.CENTER_HEIGHT` |
| `CubeSphereCells.neighbours` | `CubeSphereCells.neighbors` |
| `RasterGrid.centred(centre=)` | `RasterGrid.centered(center=)` |
| keyword `centre=` (`FlatSource`, `Surface.offset`, `Surface.locate`), `metres=` (`Surface.expand`) | `center=`, `meters=` |
| `MeshMaterial(colour=)`, `MeshMaterial.colour` | `MeshMaterial(color=)`, `MeshMaterial.color` |
| `read_icq(metres_per_unit=)`, `write_icq(metres_per_unit=)` | `meters_per_unit=` |
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
