# Examples

Examples in three forms, named `example_<category>_<name>`. They go from basic synthetic terrains,
to craters on those terrains, to boulders, to albedo, and then through every crater and rock model
and option: size-frequency distributions, age models, profiles, degradation, positioning and how
craters are applied; then regions, the Moon and Mars on spheres, detail on DEMs, and tiles.

Each form has a folder per category (`terrain`, `craters`, `distributions`, `ages`, `boulders`,
`albedo`, `layers`, `planets`, `sources`, `tiles`, `config`), holding that category's examples.

- `config/`: JSON configs for the `terraingen` command:
  `terraingen examples/config/<category>/<file>.json`.
  Outputs are written next to the config, or to `--output-dir`.
- `cpp/`: C++ programs that build the same terrains from library objects (sources, layers,
  crater and rock models, albedo). Built with the library when `TERRAINGEN_BUILD_EXAMPLES` is on;
  run as `build/bin/<name> [OUTPUT_DIR]`.
- `python/`: Python scripts that build the terrains from the same objects in the `terraingen`
  package: `python examples/python/<category>/<name>.py [OUTPUT_DIR]`. They need only NumPy, and
  write PNG previews next to their outputs; `python/_common.py` holds their shared helpers.

`example_config_load` (C++ and Python) is the one example that reads a JSON config; the others
build everything in code. Outputs go to `./output` unless a folder is given.

| Category | Config | C++ | Python |
| --- | --- | --- | --- |
| terrain | `example_terrain_billow`, `_cellular`, `_domain_warp`, `_fbm`, `_layered`, `_ridged`, `_roughness` | `example_terrain_fbm`, `_layered` | `example_terrain_fbm`, `_fractal_types`, `_hurst_sweep`, `_layered`, `_noise_kernels` |
| craters | `example_craters_datum`, `_degradation_diffusion`, `_degradation_none`, `_interior_replacement`, `_irregular`, `_manual`, `_max_craters`, `_profile_complex`, `_profile_martin_parkes_dunstan`, `_secondaries`, `_simple`, `_smoothed_rims`, `_spacing` | `example_craters_degradation`, `_manual`, `_profiles`, `_secondaries`, `_simple` | `example_craters_age_sweep`, `_apply_options`, `_degradation`, `_irregularity`, `_manual`, `_positioning`, `_profiles`, `_secondaries`, `_simple` |
| distributions | `example_distributions_moore_rough_mare`, `_piecewise_power_law`, `_power_law`, `_tabulated`, `_trask_equilibrium`, `_viper` | `example_distributions_csfd` | `example_distributions_csfd`, `_custom` |
| ages | `example_ages_fresh`, `_steady_state`, `_uniform` | `example_ages_models` | `example_ages_models` |
| boulders | `example_boulders_ejecta`, `_models`, `_patches`, `_rocks`, `_shapes`, `_sp8023` | `example_boulders_ejecta`, `_rocks` | `example_boulders_positioning`, `_rocks`, `_size_models` |
| albedo | `example_albedo_crater_rays`, `_oblique_rays`, `_rgb`, `_synthetic` | `example_albedo_crater_rays` | `example_albedo_crater_rays`, `_options` |
| layers | `example_layers_regions`, `_roughness`, `_scale_and_clamp` | `example_layers_custom`, `_regions` | `example_layers_custom`, `_regions` |
| planets | `example_planets_mars_bedforms`, `_mars_craters`, `_moon_body_mesh`, `_moon_site` | `example_planets_moon` | `example_planets_body_mesh`, `_mars`, `_moon_site` |
| sources |  | `example_sources_raster_detail` | `example_sources_chain`, `_raster_detail` |
| tiles |  | `example_tiles_quadtree` | `example_tiles_point_queries`, `_quadtree` |
| config |  | `example_config_load` | `example_config_load` |

The `sources` and `tiles` examples are code only: configs read real DEMs from disk (a raster
source is `{"type": "raster", "path": "dem.tif"}`) and write whole grids, so these examples write
a coarse DEM of their own first, and tile a terrain from code.

Custom models: every model and layer type is an interface you can implement, in C++ by deriving
from it and in Python by subclassing it (`SizeFrequencyDistribution`, `CraterAgeModel`,
`DegradationModel`, `CraterProfile`, `Source`, `Layer`, `Filter`, `AlbedoSource`, `AlbedoLayer`).
`example_distributions_custom`, `example_ages_models`, `example_craters_profiles` and
`example_layers_custom` show how.

Every example runs in the test suite (`ctest -L examples`).
