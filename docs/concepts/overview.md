# Overview

TerrainGen is organised in modules, each a folder of headers and sources under `terraingen/` that
builds one static library. A module depends only on the modules above it in this list:

| Module | Library | Contents | Depends on |
| --- | --- | --- | --- |
| `core` | `terraingen::core` | Logging, version and build info | |
| `raster` | `terraingen::raster` | Rasters, geotransforms, filters and resampling | |
| `noise` | `terraingen::noise` | Noise kernels and synthetic terrain | core, raster |
| `random` | `terraingen::random` | Counter-based random streams and samplers | noise |
| `distributions` | `terraingen::distributions` | Crater size-frequency distributions | random |
| `features` | `terraingen::features` | Per-cell feature placement and craters | core, raster, noise, random, distributions |
| `io` | `terraingen::io` | GeoTIFF reading and writing, without GDAL | core, raster |
| `pipeline` | `terraingen::pipeline` | JSON terrain configs and the generation pipeline | all of the above |

`terraingen::terraingen` links every module. Headers are included from the repository root, as in
`#include <terraingen/raster/resample.h>`; `terraingen/terraingen.h`, `terraingen/craters.h` and
`terraingen/distributions.h` include whole areas at once.

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
