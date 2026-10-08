# Examples

Examples in three forms, named `example_<category>_<name>`. They go from basic synthetic terrains,
to craters on those terrains, to boulders, to albedo.

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
| craters | `example_craters_simple`, `_manual`, `_smoothed_rims` | `example_craters_simple`, `_manual` | `example_craters_simple`, `_manual`, `_age_sweep` |
| boulders | `example_boulders_rocks`, `_models` | `example_boulders_rocks` | `example_boulders_rocks` |
| albedo | `example_albedo_synthetic`, `_crater_rays` | `example_albedo_crater_rays` | `example_albedo_crater_rays` |
| config | | `example_config_load` | `example_config_load` |

Every example runs in the test suite (`ctest -L examples`).
