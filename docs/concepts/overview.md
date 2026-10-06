# Overview

TerrainGen is organised in modules, each a folder of headers and sources under `terraingen/` that
builds one static library. A module depends only on the modules above it in this list:

| Module | Library | Contents | Depends on |
| --- | --- | --- | --- |
| `util` | `terraingen::util` | Logging, version, geometry and surface cells | |
| `raster` | `terraingen::raster` | Rasters, geotransforms, filters, resampling, and GeoTIFF reading and writing without GDAL | util |
| `noise` | `terraingen::noise` | Noise kernels (header-only) | |
| `random` | `terraingen::random` | Counter-based random streams and samplers | noise |
| `distribution` | `terraingen::distribution` | Size-frequency distributions | random |
| `sources` | `terraingen::sources` | Where heights come from: synthetic terrain from layered noise | util, raster, noise |
| `layers` | `terraingen::layers` | Relief added on top: craters (populations, profiles, ages, degradation, stamping) | util, raster, noise, random, distribution |
| `filters` | `terraingen::filters` | Transforms of the heights so far: crater rim smoothing | layers, raster |
| `bodies` | `terraingen::bodies` | Calibrations for particular bodies: the Moon's chronology, production, equilibrium and surface presets | distribution, layers |
| `pipeline` | `terraingen::pipeline` | JSON terrain configs and the generation pipeline | all of the above |

A terrain is built as a source, then layers, then filters. The general models live in `layers/` and
`filters/`, and `bodies/<name>/` only calibrates them, so supporting another body means adding a folder
there, not changing the models.

`terraingen::terraingen` links every module. Headers are included from the repository root, as in
`#include <terraingen/raster/resample.h>`, and `terraingen/terraingen.h` includes the whole library.
The build info, `GetBuildInfo()`, comes from `terraingen::build_info`, which sits above every module
so that it changes whenever any of them is rebuilt.

The noise kernels in `noise/kernels/` are header-only and use only integer hashing and simple
floating-point arithmetic, so they give bit-identical results across platforms and port directly
to GPU shading languages. Noise and random numbers are counter-based (PCG and Philox hashes), so
every value depends only on its position and seed, never on evaluation order.

## Repository layout

| Folder | Contents |
| --- | --- |
| `terraingen/` | The library: one folder per module, and the Python bindings in `terraingen/python/` |
| `tests/` | Tests by kind: `unit/` and `statistical/` mirror the modules; `golden/`, `integration/` and `python/` |
| `validation/` | Checks against published reference data, with the scripts that produce it |
| `cmake/` | Build info, testing and packaging helpers, and fetched dependencies |
| `third_party/` | Vendored dependencies (nlohmann/json, miniz) |
| `docs/` | This documentation |

## Tests

`ctest --test-dir build` runs every test; labels select a kind with `ctest -L <label>`:

| Label | Tests |
| --- | --- |
| `unit` | Behaviour of each module, including the Python bindings |
| `statistical` | Sampled distributions against tolerances derived from the sample size |
| `golden` | Noise values compared bit for bit with recorded values |
| `integration` | Install, consumer build, Debian package and build info |
| `validation` | Results against published reference data |
