"""Writes docs/api/python.md: every name terraingen exports, grouped by topic, after the guide to
writing models in Python (docs/api/python_subclassing.md). Run it after adding a binding:

    PYTHONPATH=build/python python docs/generate_python_api.py
"""
import inspect
import pathlib
import sys

import terraingen

SECTIONS = [
    ("Configs and the pipeline", "Load, validate, generate and write terrains from JSON configs, or build their objects.",
     ["Config", "ConfigError", "GeneratedTerrain", "config_catalog", "config_schema", "generate_terrain", "write_outputs", "build_terrain", "build_albedo"]),
    ("Terrains and evaluation", "Compose a source, layers and filters, and evaluate them over grids, tiles or points.",
     ["Terrain", "EvaluatedTerrain", "evaluate", "evaluate_at", "filter_margin", "RasterGrid", "LayerContext"]),
    ("Surfaces and cells", "Planes and spheres, bodies, and the cells that tile them.",
     ["Surface", "Body", "body_sphere", "mean_radius", "body_named", "SurfaceCells", "PlanarCells", "CubeSphereCells", "CellIndex"]),
    ("Sources", "Where heights come from.", ["Source", "FlatSource", "GeoTiffSource", "SourceChain", "ResampleMethod"]),
    ("Synthetic terrain and noise", "Layered fractal noise and the kernels it is made of.",
     ["NoiseRelief", "SyntheticTerrainParams", "NoiseLayer", "FractalParams", "NoiseBasis", "FractalType", "LayerBlend", "MAX_NOISE_LAYERS",
      "generate_synthetic_terrain", "fill_synthetic_terrain", "evaluate_noise_layers", "evaluate_noise_layers3", "evaluate_noise_layer",
      "domain_warp2", "gain_from_hurst", "fractal2", "fractal3", "basis_noise2", "basis_noise3", "has_basis_noise3", "perlin_noise2",
      "perlin_noise3", "simplex_noise2", "value_noise2", "value_noise3", "worley2"]),
    ("Layers", "Relief added to a terrain, and the base class for your own.",
     ["Layer", "FunctionLayer", "ScaleLayer", "ClampLayer", "RegionLayer", "RegionShape", "BlendShape", "ResolutionFade",
      "RoughnessLayer", "RoughnessOptions", "perlin_structure_function", "perlin3_structure_function", "BedformLayer",
      "BedformOptions", "mars_large_ripples", "mars_transverse_aeolian_ridges", "ExposureField", "ExposureParams"]),
    ("Crater size-frequency distributions", "Crater and rock densities, production functions and chronologies.",
     ["SizeFrequencyDistribution", "SfdKind", "PowerLawSfd", "PowerLawSegment", "PiecewisePowerLawSfd", "TabulatedSfd", "LogPolynomialSfd",
      "SummedSfd", "MinimumSfd", "SplicedSfd", "GolombekRappSfd", "ProductionSurface", "power_law_coefficient_from_kilometres",
      "trask_equilibrium", "hartmann_equilibrium", "MooreTerrain", "moore_distribution", "viper_specification",
      "neukum_production_function", "extended_neukum_production_function", "neukum_lunar_chronology", "neukum_lunar_chronology_rate",
      "ivanov_mars_production_function", "ivanov_mars_chronology", "ivanov_mars_chronology_rate", "exponential_integral",
      "SurfaceModel", "lunar_surface", "mars_surface"]),
    ("Crater ages, degradation and profiles", "How old craters are and how their shapes change.",
     ["CraterAgeModel", "FreshAges", "UniformAges", "SteadyStateAges", "DegradationModel", "NoDegradation", "FreshnessDegradation",
      "DiffusionDegradation", "PowerLawDiffusivity", "fassett_thomson_2014_diffusivity", "fassett_2022_hartmann_diffusivity",
      "fassett_2022_trask_diffusivity", "fassett_thomson_2014_diffusion_age", "CraterMorphometry", "CraterState",
      "lunar_fresh_crater_morphometry", "lunar_fresh_crater_depth_ratio", "mars_fresh_crater_morphometry", "CraterProfile",
      "FassettThomsonProfile", "FassettThomsonRim", "MartinParkesDunstanProfile", "ComplexCraterProfile", "ComplexCraterShape",
      "KilometrePowerLaw", "lunar_complex_crater_shape", "mars_complex_crater_shape", "DiffusedProfile"]),
    ("Craters", "Crater layers, crater lists and the functions that generate, place and apply them.",
     ["CraterModels", "CraterLayer", "CraterLayerOptions", "CraterGenerationOptions", "CraterSpacing", "CraterApplyOptions",
      "CraterOrder", "CraterDatum", "CraterBlend", "CraterIrregularity", "RimRoughnessModel", "ValueDistribution",
      "ValueDistributionKind", "CraterOutline", "Crater", "CraterAttributes", "CraterList", "CraterBand", "crater_bands",
      "generate_craters", "min_diameter_for_count", "manual_crater_id", "apply_craters", "crater_apply_order", "crater_blend_weight",
      "resolution_weight", "compute_crater_attributes", "fit_crater_datum", "craters_conflict", "crater_takes_precedence",
      "thin_by_spacing", "spacing_reach", "outline_of", "max_outline_scale", "SecondaryCraterLayer", "SecondaryCraterOptions",
      "singer_max_secondary_diameter", "secondary_onset_range"]),
    ("Rocks and boulders", "Rock layers, rock size models and 3D boulder shapes.",
     ["RockLayer", "RockLayerOptions", "RockPatches", "RockEjecta", "EjectaProfile", "EjectaAging", "RockRelief", "RockModels",
      "RockModelCurve", "sp8023_intercrater_blocks", "sp8023_crater_blocks", "SynthterrainRocks", "synthterrain_rock_distribution",
      "rock_candidates", "boulder_throw_distance", "Rock", "add_rock", "BoulderStyle", "ValueRange", "BaseSolid",
      "default_boulder_styles", "BoulderShapeOptions", "BoulderShape", "generate_boulder_shape", "boulder_shape_seed",
      "BoulderModel", "BoulderPlacement", "place_boulder", "boulder_mesh", "placement_at_site", "top_surface"]),
    ("Filters", "Transforms of the heights after every layer.",
     ["Filter", "RimSmoothingFilter", "RimSmoothing", "smooth_crater_rims"]),
    ("Albedo", "Surface albedo: synthetic colour, maps from orbit, and bright crater material.",
     ["Albedo", "AlbedoSource", "AlbedoLayer", "AlbedoContext", "SyntheticAlbedo", "RasterAlbedo", "RasterAlbedoOptions",
      "AlbedoDetail", "CraterAlbedo", "CraterAlbedoOptions", "CraterRays", "ObliqueImpacts", "RayRetention", "CraterImpact",
      "evaluate_albedo", "evaluate_albedo_at", "OpticalMaturityOrigin", "optical_maturity", "reflectance_at_maturity", "hsv_to_rgb"]),
    ("Tiles and whole bodies", "Quadtree tiles, tile caches and meshes of whole bodies.",
     ["TileScheme", "TileKey", "TileCache", "evaluate_tile", "evaluate_tiles", "body_mesh"]),
    ("Meshes", "Triangle meshes, transforms and glTF scenes.",
     ["TriangleMesh", "RigidTransform", "SceneNode", "write_glb_scene", "base_solid_mesh", "icosphere"]),
    ("Rasters and GeoTIFF", "Rasters shared with NumPy, GeoTIFF reading and writing, and resampling.",
     ["Raster", "GeoTransform", "RasterStats", "compute_stats", "read_geotiff", "read_geotiff_info", "write_geotiff", "GeoTiffInfo",
      "GeoTiffWriteOptions", "GeoKeys", "geographic_sphere_keys", "BandScaling", "TiffCompression", "TiffSampleFormat", "GeoTiffError",
      "gaussian_blur", "resample", "resample_to", "sample_raster"]),
    ("Hashing and random numbers", "Counter-based random numbers, as the generators use.",
     ["pcg_hash", "pcg3d_hash", "philox4x32", "hash_to_unit_float", "RandomStream", "TabulatedDistribution", "sample_exponential",
      "sample_normal", "sample_poisson", "sample_truncated_power_law"]),
    ("Backends", "Choosing where evaluation runs.",
     ["Backend", "BackendStatus", "backends", "backend_named", "default_backend", "set_default_backend", "resolve_backend"]),
    ("Validation", "Check settings without building anything; constructors raise the same errors.",
     sorted(n for n in terraingen.__all__ if n.startswith("validate_"))),
    ("Logging and version", "Log records and build information.",
     ["set_log_level", "get_log_level", "set_log_sink", "use_python_logging", "version", "api_version", "build_info", "__version__"]),
]

listed = [n for _, _, names in SECTIONS for n in names]
missing = sorted(set(terraingen.__all__) - set(listed))
extra = sorted(set(listed) - set(terraingen.__all__))
if missing or extra:
    sys.exit(f"missing {missing}, unknown {extra}")

lines = ["# Python API", "",
         "The `terraingen` package wraps the C++ library with nanobind: every public class and function",
         "of the C++ library has a Python counterpart (a test checks this against the headers). Rasters",
         "share their cells with NumPy without copying, long-running functions release the GIL, and the",
         "base classes (distributions, age and degradation models, profiles, sources, layers, filters and",
         "albedo) can be subclassed in Python. Build the extension module first (`pip install -e .`, or a",
         "CMake build in `build/`) so the reference can be generated.", "",
         "```{eval-rst}", ".. automodule:: terraingen", "   :no-members:", "```", ""]
lines += (pathlib.Path(__file__).parent / "api" / "python_subclassing.md").read_text().splitlines() + [""]
for title, intro, names in SECTIONS:
    lines += [f"## {title}", "", intro, "", "```{eval-rst}"]
    for name in names:
        obj = getattr(terraingen, name)
        kind = "autoclass" if inspect.isclass(obj) else "autofunction" if callable(obj) else "autodata"
        lines.append(f".. {kind}:: terraingen.{name}")
    lines += ["```", ""]
open(pathlib.Path(__file__).parent / "api" / "python.md", "w").write("\n".join(lines).rstrip() + "\n")
print(len(listed), "names in", len(SECTIONS), "sections")
