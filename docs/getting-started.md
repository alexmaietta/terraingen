# Getting Started

These examples generate rolling synthetic terrain; the C++ one then adds the craters of an old
lunar surface and writes a GeoTIFF.

## Python

```python
import numpy as np

import terraingen as tg

layer = tg.NoiseLayer()
layer.fractal = tg.FractalParams(seed=1, wavelength=500.0, amplitude=20.0)
params = tg.SyntheticTerrainParams()
params.layers = [layer]

heights = tg.generate_synthetic_terrain(1024, 1024, pixel_size=1.0, params=params)
print(heights.transform)          # GeoTransform(-512, 1, 0, 512, 0, -1): centred on the origin
print(tg.compute_stats(heights))  # min, max, mean and valid count

array = np.asarray(heights)       # a (rows, cols) float32 view of the cells, without copying
```

Heights are in metres on a north-up grid of square pixels centred on the world origin.

## C++

```cpp
#include <terraingen/raster/geotiff.h>
#include <terraingen/terraingen.h>

#include <cstdio>

int main() {
    using namespace terraingen;

    // Rolling terrain: one layer of Perlin fBm with a 500 m wavelength and 20 m amplitude.
    SyntheticTerrainParams params;
    params.layers[0].fractal.wavelength = 500.0f;
    params.layers[0].fractal.amplitude = 20.0f;
    Raster<float> heights = GenerateSyntheticTerrain(1024, 1024, 1.0, params);

    // Craters of a 3.5 Gyr old lunar surface between 5 m and 200 m, with the Fassett and Thomson
    // (2014) profile, degraded by age.
    const SurfaceModel surface = LunarSurface(3.5);
    const FassettThomsonProfile profile;
    const Bounds2d region{-512.0, -512.0, 512.0, 512.0};
    const CraterList craters =
        GenerateCraters(PlanarCells(), region, *surface.density, *surface.ages, /*seed=*/7,
                        {.dMin = 5.0, .dMax = 200.0, .marginRadii = profile.ReachRadii()});
    ApplyCraters(heights, craters, profile, FreshnessDegradation(surface.ages));

    WriteGeoTiff("terrain.tif", heights);
    std::printf("%zu craters\n", craters.Size());
}
```

Build it against an installed TerrainGen:

```cmake
cmake_minimum_required(VERSION 3.22)
project(terrain_example LANGUAGES CXX)

find_package(terraingen REQUIRED)

add_executable(terrain_example main.cpp)
target_link_libraries(terrain_example PRIVATE terraingen::terraingen)
```

Craters are generated per cell from independent random substreams, so the same seed gives the
same craters however the region is tiled, and lowering `dMin` adds small craters without moving
larger ones.

## Next steps

- {doc}`concepts/index`
- {doc}`api/index`
