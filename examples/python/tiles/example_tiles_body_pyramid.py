"""A tile pyramid of a whole body, as a globe renderer streams it.

`TileScheme.whole_body` is a longitude-latitude quadtree over a sphere: level L has 2^(L+1) x 2^L
tiles covering every longitude and latitude once. A `TileCache` evaluates tiles on demand and keeps
the most recently used up to a byte budget, so a viewer can ask for the same tiles again cheaply.
This example writes levels 0 to 2 of the Moon as GeoTIFFs, one folder per level, and a mosaic of
level 2.

    python example_tiles_body_pyramid.py [OUTPUT_DIR]
"""

import pathlib
import sys
import time

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, normalize, output_dir, write_png

CELLS = 64


def main():
    out = output_dir()
    moon = terraingen.body_sphere(terraingen.Body.MOON)
    noise = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=8, wavelength=400000.0, amplitude=3000.0, octaves=6))], surface=moon
    )
    ages = terraingen.UniformAges(4.0)
    models = terraingen.CraterModels(
        terraingen.PowerLawSfd(1e-15, 2.0), ages, terraingen.FreshnessDegradation(ages), terraingen.FassettThomsonProfile()
    )
    basins = [terraingen.Crater(0.0, 0.0, 300000.0, age_gyr=3.9), terraingen.Crater(-60.0, 30.0, 200000.0, age_gyr=3.5)]
    craters = terraingen.CraterLayer(
        models,
        terraingen.CraterLayerOptions(generate=False, surface=moon, generation=terraingen.CraterGenerationOptions(manual=basins)),
    )
    terrain = terraingen.Terrain(terraingen.FlatSource(8, 8, 1.0), [noise, craters], surface=moon)
    scheme = terraingen.TileScheme.whole_body(CELLS)
    cache = terraingen.TileCache(terrain, scheme, max_bytes=64 << 20)

    for level in range(3):
        keys = terraingen.TileScheme.body_tiles(level)
        folder = out / "example_tiles_body_pyramid" / str(level)
        folder.mkdir(parents=True, exist_ok=True)
        start = time.perf_counter()
        for key in keys:
            terraingen.write_geotiff(folder / f"{key.x}_{key.y}.tif", cache.get(key).heights)
        spacing_km = scheme.spacing(level) * moon.unit_length / 1000
        print(f"level {level}: {len(keys)} tiles at {spacing_km:.0f} km spacing in {time.perf_counter() - start:.2f} s")

    start = time.perf_counter()
    rows = [np.hstack([cache.get(terraingen.TileKey(2, x, y)).heights.values for x in range(8)]) for y in range(4)]
    print(f"level 2 again from the cache in {(time.perf_counter() - start) * 1000:.0f} ms; {cache.size >> 10} kB held")
    mosaic = np.vstack(rows)
    print("wrote", write_png(out / "example_tiles_body_pyramid.png", normalize(mosaic)))


if __name__ == "__main__":
    main()
