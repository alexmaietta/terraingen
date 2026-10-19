# Examples

Examples in three forms, named `example_<category>_<name>`. They go from basic synthetic terrains,
to craters on those terrains, to boulders, to albedo, and then through every crater and boulder model
and option: size-frequency distributions, age models, profiles, degradation, positioning and how
craters are applied; then regions, the Moon and Mars on spheres, detail on DEMs, and tiles.

Each form has a folder per category (`terrain`, `craters`, `distributions`, `ages`, `boulders`,
`albedo`, `layers`, `planets`, `sources`, `tiles`, `config`), holding that category's examples.

- `config/`: JSON configs for the `terraingen` command:
  `terraingen examples/config/<category>/<file>.json [--output-dir DIR]`.
  Outputs are written next to the config, or to `--output-dir`.
- `cpp/`: C++ programs that build the same terrains from library objects (sources, layers,
  crater and boulder models, albedo). Built with the library when `TERRAINGEN_BUILD_EXAMPLES` is on.
- `python/`: Python scripts that build the terrains from the same objects in the `terraingen`
  package. They need only NumPy, and write PNG previews next to their outputs; `python/_common.py`
  holds their shared helpers.

`example_config_load` (C++ and Python) is the one example that reads a JSON config; the others
build everything in code.

## Running the examples

Each C++ example is its own program, and `terraingen-examples` runs any of them by name:

```sh
build/bin/example_craters_simple [--output-dir DIR] [--test] [--no-save]
build/bin/terraingen-examples --list
build/bin/terraingen-examples craters_simple [--viewer window|null] [--output-dir DIR] [--test]
```

Python examples run as scripts, or by name through the runner in `examples/python`:

```sh
python examples/python/craters/example_craters_simple.py [--viewer window|null] [--output-dir DIR] [--test]
python examples/python                  # list the examples
python examples/python craters_simple --help
```

Options:

- `--output-dir DIR`: where outputs go (default `./output`).
- `--test`: run the example's checks after generating.
- `--save`, `--no-save`: write the outputs or not. By default outputs are written only without a
  viewer. The single C++ programs take only `--no-save`.
- `--viewer window|null` (`terraingen-examples` and Python): `window` shows the terrain in a
  viewer with an Examples panel to switch to the others; `null` runs without one. The C++ viewer
  needs a build with the GUI and the Python one needs Ernie's Python package; without them the
  default is `null`.
- `--headless` (`terraingen-examples` and Python): render the viewer without a window.

## Examples by category

| Category | Config | C++ | Python |
| --- | --- | --- | --- |
| terrain | `example_terrain_billow`, `_cellular`, `_domain_warp`, `_fbm`, `_hierarchy`, `_layered`, `_ridged`, `_roughness`, `_voronoi` | `example_terrain_fbm`, `_layered` | `example_terrain_fbm`, `_fractal_types`, `_hurst_sweep`, `_layered`, `_noise_kernels` |
| craters | `example_craters_datum`, `_degradation_diffusion`, `_degradation_none`, `_interior_replacement`, `_irregular`, `_manual`, `_manual_depth`, `_max_craters`, `_profile_complex`, `_profile_martin_parkes_dunstan`, `_rim_smoothing_soft`, `_secondaries`, `_secondary_rays`, `_simple`, `_smoothed_rims`, `_spacing` | `example_craters_degradation`, `_manual`, `_manual_depth`, `_profiles`, `_secondaries`, `_simple` | `example_craters_age_sweep`, `_apply_options`, `_degradation`, `_irregularity`, `_manual`, `_manual_depth`, `_positioning`, `_profiles`, `_rim_smoothing`, `_secondaries`, `_simple` |
| distributions | `example_distributions_moore_rough_mare`, `_piecewise_power_law`, `_power_law`, `_tabulated`, `_trask_equilibrium`, `_viper` | `example_distributions_csfd` | `example_distributions_csfd`, `_custom` |
| ages | `example_ages_fresh`, `_freshness`, `_steady_state`, `_uniform` | `example_ages_models` | `example_ages_models` |
| boulders | `example_boulders_buried`, `_ejecta`, `_fragments`, `_exposure`, `_layer`, `_manual`, `_models`, `_patches`, `_scene`, `_shapes`, `_sp8023` | `example_boulders_ejecta`, `_layer`, `_manual`, `_shapes` | `example_boulders_exposure`, `_layer`, `_manual`, `_positioning`, `_shapes`, `_size_models` |
| albedo | `example_albedo_blast_zone`, `_crater_rays`, `_fresh_crater`, `_moon_site`, `_new_crater`, `_oblique_rays`, `_rgb`, `_synthetic` | `example_albedo_crater_rays` | `example_albedo_crater_rays`, `_moon_site`, `_options` |
| layers | `example_layers_regions`, `_roughness`, `_scale_and_clamp` | `example_layers_custom`, `_regions` | `example_layers_custom`, `_regions` |
| planets | `example_planets_asteroid_body_mesh`, `_mars_bedforms`, `_mars_craters`, `_mars_dunes`, `_mars_exposure`, `_mars_pavement`, `_mars_sand_fill`, `_mars_sand_shadows`, `_mars_strata`, `_mars_ventifacts`, `_mercury_site`, `_moon_body_mesh`, `_moon_site` | `example_planets_moon` | `example_planets_body_mesh`, `_mars`, `_mars_dunes`, `_mars_exposure`, `_mercury`, `_moon_site` |
| sources |  | `example_sources_raster_detail` | `example_sources_chain`, `_raster_detail` |
| tiles |  | `example_tiles_body_pyramid`, `_quadtree`, `_rover_contacts` | `example_tiles_body_pyramid`, `_point_queries`, `_quadtree`, `_rover_contacts` |
| config |  | `example_config_load` | `example_config_load` |

The `sources` and `tiles` examples are code only: configs read real DEMs from disk (a raster
source is `{"type": "raster", "path": "dem.tif"}`) and write whole grids, so these examples write
a coarse DEM of their own first, and tile a terrain from code.

Custom models: every model and layer type is an interface you can implement, in C++ by deriving
from it and in Python by subclassing it (`SizeFrequencyDistribution`, `CraterAgeModel`,
`DegradationModel`, `CraterProfile`, `Source`, `Layer`, `AlbedoSource`, `AlbedoLayer`).
`example_distributions_custom`, `example_ages_models`, `example_craters_profiles` and
`example_layers_custom` show how.

Every example runs in the test suite with its checks (`ctest -L examples`).
