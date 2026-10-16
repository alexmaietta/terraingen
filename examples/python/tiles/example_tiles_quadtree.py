"""Tiles and levels of detail: evaluate a terrain piece by piece, at any resolution.

A `TileScheme` is a quadtree of north-up tiles, each `cells` x `cells` samples, halving in size
(and spacing) at each level, as a renderer's level-of-detail quadtree does. Every tile equals the
same cells of any larger grid, to the bit, so tiles can be generated independently, in any order,
on any machine. This example tiles a cratered terrain at three levels, checks the finest tiles
against the whole grid, and writes a montage of one area at each level.

    python example_tiles_quadtree.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, montage, write_png


class Example(_common.Example):
    def generate(self):
        production = terraingen.extended_neukum_production_function()
        equilibrium = terraingen.trask_equilibrium()
        density = terraingen.MinimumSfd(
            [terraingen.ProductionSurface(production, 3.5, terraingen.neukum_lunar_chronology), equilibrium]
        )
        ages = terraingen.SteadyStateAges(production, equilibrium, 3.5, terraingen.neukum_lunar_chronology)
        models = terraingen.CraterModels(
            density,
            ages,
            terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
            terraingen.FassettThomsonProfile(),
        )
        craters = terraingen.CraterLayer(
            models,
            terraingen.CraterLayerOptions(
                generation=terraingen.CraterGenerationOptions(min_diameter=4.0, max_diameter=200.0)
            ),
        )
        noise = terraingen.NoiseRelief(
            [terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=400.0, amplitude=15.0))]
        )
        grid = terraingen.RasterGrid.centred(512, 512, 1.0)
        terrain = terraingen.Terrain(terraingen.FlatSource(512, 512, 1.0), [noise, craters])
        scheme = terraingen.TileScheme.covering(grid, cells=128)

        self.finest = scheme.level_for(1.0)
        self.whole = terraingen.evaluate(terrain, grid=grid)
        whole = self.whole.values
        self.keys = scheme.tiles_covering(grid.bounds, self.finest)
        self.tiles = terraingen.evaluate_tiles(terrain, scheme, self.keys)
        self.same = all(
            np.array_equal(
                tile.values,
                whole[int(round(grid.transform.origin_y - scheme.grid(key).transform.origin_y)) :][
                    :128, int(round(scheme.grid(key).transform.origin_x - grid.transform.origin_x)) :
                ][:, :128],
            )
            for key, tile in zip(self.keys, self.tiles, strict=True)
        )
        print(
            f"level {self.finest}: {len(self.keys)} tiles of {scheme.tile_size(self.finest):.0f} m, equal to the whole grid: {self.same}"
        )
        self.show("whole grid", self.whole)

        self.shades = []
        self.crater_counts = []
        for level in (self.finest - 2, self.finest - 1, self.finest):
            key = scheme.tile_at(-1.0, 1.0, level)
            tile = terraingen.evaluate_tile(terrain, scheme, key)
            self.crater_counts.append(len(terraingen.placed_craters(terrain, grid=scheme.grid(key)).craters))
            print(
                f"level {level}: tile {key}, {scheme.spacing(level):.0f} m spacing, {self.crater_counts[-1]} craters"
            )
            self.shades.append(hillshade(tile.values, scheme.spacing(level)))
            self.show(f"level {level} tile ({key.x}, {key.y})", tile)

    def save(self, out):
        print("wrote", write_png(out / "example_tiles_quadtree.png", montage(self.shades, columns=3)))

    def test_final(self):
        assert self.finest == 2 and len(self.keys) == 16, "level 2 has 16 tiles over the 512 m grid"
        assert all(tile.shape == (128, 128) for tile in self.tiles), "every tile is 128 x 128"
        assert self.same, "every tile equals the same cells of the whole grid, to the bit"
        assert np.isfinite(self.whole.values).all(), "every height is finite"
        assert len(self.shades) == 3 and all(shade.shape == (128, 128) for shade in self.shades), (
            "one 128 x 128 tile at each of three levels"
        )
        assert all(count > 0 for count in self.crater_counts), "every tile has craters"


if __name__ == "__main__":
    _common.main(Example)
