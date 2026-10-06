# Overview

TerrainGen is organised in modules, each a folder of headers and sources under `terraingen/` that
builds one static library. A module depends only on the modules above it in this list:

| Module | Library | Contents | Depends on |
| --- | --- | --- | --- |
| `util` | `terraingen::util` | Logging, version, geometry and surface cells | |
| `raster` | `terraingen::raster` | Rasters, geotransforms, filters, resampling, and GeoTIFF reading and writing without GDAL | util |
| `noise` | `terraingen::noise` | Noise kernels (header-only) | |
| `random` | `terraingen::random` | Counter-based random streams and samplers | noise |
| `distribution` | `terraingen::distribution` | Size-frequency distributions of craters and rocks | random |
| `sources` | `terraingen::sources` | Where heights come from: synthetic terrain from layered noise | util, raster, noise |
| `layers` | `terraingen::layers` | Relief added on top: craters (populations, profiles, ages, degradation, stamping), rocks, fine roughness, the bedrock exposure field, and pointwise transforms and regions | util, raster, noise, random, distribution |
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

## Composing a terrain

A `Terrain` is a `Source`, a list of `Layer`s and a list of `Filter`s. `Evaluate` fills a grid from
the source, adds each layer's relief in order, then runs each filter in order:

```cpp
terraingen::Terrain terrain;
terrain.source = std::make_shared<terraingen::FlatSource>(1024, 1024, 1.0);
terrain.layers.push_back(std::make_shared<terraingen::NoiseRelief>(noise));
terrain.layers.push_back(std::make_shared<terraingen::CraterLayer>(models, options));
terrain.filters.push_back(std::make_shared<terraingen::RimSmoothingFilter>(smoothing));

auto whole = terraingen::Evaluate(terrain);           // over the source's grid
auto tile = terraingen::Evaluate(terrain, tileGrid);  // any grid on the same lattice
```

- **Sources** fill a grid: `FlatSource` (a level datum), `GeoTiffSource` (a DEM, optionally
  resampled, with its band's GDAL scale and offset applied), or `SourceChain`, which fills each
  source's gaps from the next.
- **Layers** add relief and record what they placed (`TerrainFeatures`, such as the craters):
  `NoiseRelief`, `CraterLayer`, `RockLayer` and `RoughnessLayer`. Layers can also reshape the heights so
  far, point by point: `ScaleLayer`, `ClampLayer`, `FunctionLayer`, and `RegionLayer`, which applies
  another layer only inside a rectangle or circle, feathered at its edge.
- **Filters** transform the heights using those features: `RimSmoothingFilter`.

Layers are pure functions of position, so a tile evaluated alone equals the same cells of the whole
terrain. Filters may read neighbouring cells, so tiles need a margin to match. Derive from `Source`,
`Layer` or `Filter` to add your own.

Every evaluation takes a sample spacing. Craters, rocks, and the rim smoothing around craters, fade with it
(`ResolutionFade`): absent below 2 samples across, whole from 4, smooth in between. A crater therefore
changes continuously between coarse and fine levels of detail instead of popping in or out, and size
bands too small to resolve at a spacing are not generated at all.

Layers are given the ground beneath them: the source and the layers before them, readable at any
point (`Ground`, provided by `TerrainGround`). Craters cut to a datum fit it to that ground plus the
craters before them, on a fixed lattice around each crater, so a crater landing in an earlier bowl sits
on its floor while the result stays the same on every grid, tile and point.

Rocks follow the Golombek and Rapp (1997) size distribution from a coverage k that varies in patches,
rises toward the rims of fresh craters (their ejecta, from a crater layer before the rocks) and rises
where bedrock shows (`ExposureField`, read from how far the ground stands above its surroundings and
how steep it is). Each rock is recorded with its size, height, burial and tilt in
`TerrainFeatures::rocks`, and drawn into the heights as a dome, or left out of them
(`RockRelief::None`) for programs that place each rock as a body or mesh of its own.

Fine roughness is given as planetary surveys measure it (Shepard et al. 2001): an RMS slope over a
baseline and a Hurst exponent, how the RMS height difference grows with the baseline. `RoughnessLayer`
builds it from octaves of noise whose amplitudes are calibrated so that the RMS slope over the baseline
is the one asked for, and each octave fades out with the spacing like craters do.

`EvaluateAt(terrain, x, y, spacing)` evaluates a single point, to the same bits as the grid cell
centred there, and is safe to call from many threads, as a soil model querying ground heights node by
node does. Crater cells are kept in a bounded cache (`CraterCellCache`), so neighbouring tiles and
points generate each cell once. Point queries cannot use the `max_craters` cap, which ranks craters over a
whole grid, and run the source and layers only, since filters read neighbouring cells.

## Tiles and threads

`Evaluate(terrain, grid, {.threads = 0})` splits every layer and filter over all hardware threads and
gives the same bits as one thread. For level-of-detail rendering and streaming, a `TileScheme` is a
planar quadtree of tiles: level 0 is the coarsest, each level halves the tile side and the spacing, and
`TileScheme::Covering(grid)` lines one level up with a source's own cells.

```cpp
const auto scheme = terraingen::TileScheme::Covering(terrain.source->Grid(), 256);
auto tile = terraingen::EvaluateTile(terrain, scheme, {level, x, y});
auto many = terraingen::EvaluateTiles(terrain, scheme, keys);  // in parallel
terraingen::TileCache cache(terrain, scheme, 1 << 30);          // evaluated once, kept within 1 GiB
```

A tile is a piece of the unbounded terrain: filters such as rim smoothing are evaluated with a margin
around it (`FilterMargin`) and cropped, so tiles equal the same cells of any larger grid on the same
lattice. Raster sources copy their own cells exactly and interpolate onto tiles at other levels.

JSON configurations describe the same composition:

```json
{"schema_version": 1,
 "source": {"type": "flat", "width": 1024, "height": 1024, "pixel_size": 1},
 "layers": [{"type": "noise", "stack": [{"wavelength": 500, "amplitude": 20}]},
            {"type": "craters", "density": {"type": "lunar_surface", "age_gyr": 3.5}},
            {"type": "rocks", "coverage": 0.02, "ejecta": {"min_crater_diameter": 20},
             "exposure": {}},
            {"type": "roughness", "rms_slope": 0.15, "baseline": 0.25, "hurst": 0.8}],
 "filters": [{"type": "rim_smoothing"}],
 "outputs": [{"type": "heightmap", "path": "terrain.tif"}]}
```

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
