# Examples

Examples in three forms, named `example_<category>_<name>`. They go from basic synthetic terrains,
to craters on those terrains, to boulders, to albedo, and then through every crater and boulder model
and option: size-frequency distributions, age models, profiles, degradation, positioning and how
craters are applied; then regions, the Moon and Mars on spheres, detail on DEMs, and point queries.

Each form has a folder per category (`terrain`, `craters`, `distributions`, `ages`, `boulders`,
`albedo`, `layers`, `planets`, `sources`, `queries`, `config`), holding that category's examples.

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

The files in each category folder are the examples; `build/bin/terraingen-examples --list` and
`python examples/python` list the C++ and Python ones by name.

The `sources` and `queries` examples are code only: configs read real DEMs from disk (a raster
source is `{"type": "raster", "path": "dem.tif"}`) and write whole grids, so these examples write
a coarse DEM of their own first, and query a terrain at points from code.

Custom models: every model and layer type is an interface you can implement, in C++ by deriving
from it and in Python by subclassing it (`SizeFrequencyDistribution`, `CraterAgeModel`,
`DegradationModel`, `CraterProfile`, `Source`, `Layer`, `AlbedoSource`, `AlbedoLayer`).
`example_distributions_custom`, `example_ages_models`, `example_craters_profiles` and
`example_layers_custom` show how.

Every example runs in the test suite with its checks (`ctest -L examples`).
