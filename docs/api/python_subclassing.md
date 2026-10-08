## Writing your own models in Python

Each base class can be subclassed in Python: `SizeFrequencyDistribution` (`cumulative_at`),
`CraterAgeModel` (`lifetime_at`), `DegradationModel` (`state`), `CraterProfile` (`height`,
`reach_radii`), `Source` (`grid`, `height_at`), `Layer` (`apply_at` or `apply`), `Filter` (`apply`),
`AlbedoSource` (`bands`, `value_at`) and `AlbedoLayer` (`apply_at`); `FunctionLayer` wraps a
function. The examples `example_distributions_custom`, `example_ages_models`,
`example_craters_profiles` and `example_layers_custom` show each.

Python methods run under the GIL, so what they cost depends on how often TerrainGen calls them:

- Called once per crater, cell or grid (distributions, age and degradation models, `Layer.apply`
  and `Filter.apply` with NumPy): as fast as the built-in parts, and threads still help.
- Called once per sample on one thread (a `Layer` with only `apply_at`, a `Source` with only
  `height_at`): about a microsecond per call, and it does not speed up with threads.
- Called once per sample from several threads (a `CraterProfile`, `FunctionLayer`): the threads
  take turns on the GIL, which is slower than one thread. Evaluate such terrains with `threads=1`,
  or move the work into `apply` over the whole raster with NumPy.

Measured with `tests/benchmark/bench_python_subclasses.py` on a 256 x 256 grid (an optimized build,
best of three; milliseconds):

| Python part | 1 thread | 4 threads | 8 threads | Built-in part it replaces, 1 thread |
| --- | ---: | ---: | ---: | ---: |
| `Layer.apply_at` | 33 | 31 | 32 | 30 (4-octave noise) |
| `Layer.apply` with NumPy | 0.4 | 0.5 | 0.5 | 30 (4-octave noise) |
| `FunctionLayer` | 27 | 361 | 352 | 30 (4-octave noise) |
| `CraterProfile` | 43 | 758 | 840 | 1.1 (`FassettThomsonProfile`) |
| `DegradationModel` | 1.2 | 0.7 | 0.7 | 1.1 (`NoDegradation`) |
| `SizeFrequencyDistribution` | 1.2 | 0.6 | 0.5 | 1.2 (`PowerLawSfd`) |
