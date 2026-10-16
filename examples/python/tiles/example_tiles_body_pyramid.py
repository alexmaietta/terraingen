"""A tile pyramid of a whole body, as a globe renderer streams it.

`TileScheme.whole_body` is a longitude-latitude quadtree over a sphere: level L has 2^(L+1) x 2^L
tiles covering every longitude and latitude once. A `TileCache` evaluates tiles on demand and keeps
the most recently used up to a byte budget, so a viewer can ask for the same tiles again cheaply.
This example writes levels 0 to 2 of the Moon as GeoTIFFs, one folder per level, and a mosaic of
level 2.

    python example_tiles_body_pyramid.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys
import time

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import normalize, write_png

CELLS = 64
LEVELS = 3


class Example(_common.Example):
    def generate(self):
        moon = terraingen.body_sphere(terraingen.Body.MOON)
        noise = terraingen.NoiseRelief(
            [
                terraingen.NoiseLayer(
                    terraingen.FractalParams(seed=8, wavelength=400000.0, amplitude=3000.0, octaves=6)
                )
            ],
            surface=moon,
        )
        ages = terraingen.UniformAges(4.0)
        models = terraingen.CraterModels(
            terraingen.PowerLawSfd(1e-15, 2.0),
            ages,
            terraingen.FreshnessDegradation(ages),
            terraingen.FassettThomsonProfile(),
        )
        basins = [
            terraingen.Crater(0.0, 0.0, 300000.0, age_gyr=3.9),
            terraingen.Crater(-60.0, 30.0, 200000.0, age_gyr=3.5),
        ]
        craters = terraingen.CraterLayer(
            models,
            terraingen.CraterLayerOptions(
                generate=False, surface=moon, generation=terraingen.CraterGenerationOptions(manual=basins)
            ),
        )
        terrain = terraingen.Terrain(terraingen.FlatSource(8, 8, 1.0), [noise, craters], surface=moon)
        scheme = terraingen.TileScheme.whole_body(CELLS)
        cache = terraingen.TileCache(terrain, scheme, max_bytes=64 << 20)

        self.levels = []
        for level in range(LEVELS):
            start = time.perf_counter()
            self.levels.append(
                [(key, cache.get(key).heights) for key in terraingen.TileScheme.body_tiles(level)]
            )
            spacing_km = scheme.spacing(level) * moon.unit_length / 1000
            print(
                f"level {level}: {len(self.levels[level])} tiles at {spacing_km:.0f} km spacing in {time.perf_counter() - start:.2f} s"
            )

        start = time.perf_counter()
        rows = [
            np.hstack([cache.get(terraingen.TileKey(2, x, y)).heights.values for x in range(8)])
            for y in range(4)
        ]
        print(
            f"level 2 again from the cache in {(time.perf_counter() - start) * 1000:.0f} ms; {cache.size >> 10} kB held"
        )
        self.mosaic = np.vstack(rows)
        self.show("level 2 mosaic (equirectangular)", self.mosaic, scheme.spacing(2) * moon.unit_length)

    def save(self, out):
        for level, tiles in enumerate(self.levels):
            folder = out / "example_tiles_body_pyramid" / str(level)
            folder.mkdir(parents=True, exist_ok=True)
            for key, heights in tiles:
                terraingen.write_geotiff(folder / f"{key.x}_{key.y}.tif", heights)
        print("wrote", out / "example_tiles_body_pyramid")
        print("wrote", write_png(out / "example_tiles_body_pyramid.png", normalize(self.mosaic)))

    def test_final(self):
        for level, tiles in enumerate(self.levels):
            assert len(tiles) == 2 ** (level + 1) * 2**level, f"level {level} has 2^(L+1) x 2^L tiles"
            assert all(heights.shape == (CELLS, CELLS) for _, heights in tiles), "every tile is 64 x 64"
        assert self.mosaic.shape == (4 * CELLS, 8 * CELLS), "the level 2 mosaic is 8 x 4 tiles"
        assert np.isfinite(self.mosaic).all(), "every height is finite"
        assert -20000.0 < self.mosaic.min() and self.mosaic.max() < 20000.0, (
            "heights within 20 km of the datum"
        )
        assert np.ptp(self.mosaic) > 1000.0, "heights span kilometres"
        steps = np.abs(np.roll(self.mosaic, -1, axis=1) - self.mosaic)
        seams = (np.arange(steps.shape[1]) + 1) % CELLS == 0
        assert steps[:, seams].max() <= 1.5 * steps[:, ~seams].max() + 1.0, (
            "tile seams, the antimeridian included, are no steeper than inside the tiles"
        )


if __name__ == "__main__":
    _common.main(Example)
