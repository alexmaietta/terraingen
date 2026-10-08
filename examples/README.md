# Examples

Examples in three forms, named `example_<category>_<name>`. They go from basic synthetic terrains,
to craters on those terrains, to boulders, to albedo, and then through every crater and rock model
and option: size-frequency distributions, age models, profiles, degradation, positioning and how
craters are applied.

- `config/`: JSON configs for the `terraingen` command: `terraingen examples/config/<file>.json`.
  Outputs are written next to the config, or to `--output-dir`.
- `cpp/`: C++ programs that build the same terrains from library objects (sources, layers,
  crater and rock models, albedo). Built with the library when `TERRAINGEN_BUILD_EXAMPLES` is on;
  run as `build/bin/<name> [OUTPUT_DIR]`.
- `python/`: Python scripts that build the terrains from the same objects in the `terraingen`
  package: `python examples/python/<name>.py [OUTPUT_DIR]`. They need only NumPy, and write PNG
  previews next to their outputs.

`example_config_load` (C++ and Python) is the one example that reads a JSON config; the others
build everything in code. Outputs go to `./output` unless a folder is given.

| Category | Config | C++ | Python |
| --- | --- | --- | --- |
| terrain | `example_terrain_fbm`, `_roughness`, `_ridged`, `_billow`, `_cellular`, `_domain_warp`, `_layered` | `example_terrain_fbm`, `_layered` | `example_terrain_fbm`, `_layered`, `_fractal_types`, `_hurst_sweep`, `_noise_kernels` |
| craters | `example_craters_simple`, `_manual`, `_smoothed_rims`, `_profile_martin_parkes_dunstan`, `_profile_complex`, `_degradation_none`, `_degradation_diffusion`, `_spacing`, `_max_craters`, `_datum`, `_interior_replacement`, `_irregular`, `_secondaries` | `example_craters_simple`, `_manual`, `_profiles`, `_degradation`, `_secondaries` | `example_craters_simple`, `_manual`, `_age_sweep`, `_profiles`, `_degradation`, `_positioning`, `_apply_options`, `_irregularity`, `_secondaries` |
| distributions | `example_distributions_power_law`, `_piecewise_power_law`, `_tabulated`, `_trask_equilibrium`, `_moore_rough_mare`, `_viper` | `example_distributions_csfd` | `example_distributions_csfd`, `_custom` |
| ages | `example_ages_fresh`, `_uniform`, `_steady_state` | `example_ages_models` | `example_ages_models` |
| boulders | `example_boulders_rocks`, `_models`, `_sp8023`, `_patches`, `_ejecta`, `_shapes` | `example_boulders_rocks`, `_ejecta` | `example_boulders_rocks`, `_size_models`, `_positioning` |
| albedo | `example_albedo_synthetic`, `_crater_rays` | `example_albedo_crater_rays` | `example_albedo_crater_rays` |
| layers | | `example_layers_custom` | `example_layers_custom` |
| config | | `example_config_load` | `example_config_load` |

Custom models: every model and layer type is an interface you can implement, in C++ by deriving
from it and in Python by subclassing it (`SizeFrequencyDistribution`, `CraterAgeModel`,
`DegradationModel`, `CraterProfile`, `Source`, `Layer`, `Filter`, `AlbedoSource`, `AlbedoLayer`).
`example_distributions_custom`, `example_ages_models`, `example_craters_profiles` and
`example_layers_custom` show how.

Every example runs in the test suite (`ctest -L examples`).
