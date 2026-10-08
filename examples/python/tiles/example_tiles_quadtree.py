"""Tiles and levels of detail: evaluate a terrain piece by piece, at any resolution.

A `TileScheme` is a quadtree of north-up tiles, each `cells` x `cells` samples, halving in size
(and spacing) at each level, as a renderer's level-of-detail quadtree does. Every tile equals the
same cells of any larger grid, to the bit, so tiles can be generated independently, in any order,
on any machine. This example tiles a cratered terrain at three levels, checks the finest tiles
against the whole grid, and writes a montage of one area at each level.

    python example_tiles_quadtree.py [OUTPUT_DIR]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, montage, output_dir, write_png


def main():
    out = output_dir()
    surface = terraingen.lunar_surface(3.5)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    craters = terraingen.CraterLayer(
        models, terraingen.CraterLayerOptions(generation=terraingen.CraterGenerationOptions(min_diameter=4.0, max_diameter=200.0))
    )
    noise = terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=400.0, amplitude=15.0))])
    grid = terraingen.RasterGrid.centred(512, 512, 1.0)
    terrain = terraingen.Terrain(terraingen.FlatSource(512, 512, 1.0), [noise, craters])
    scheme = terraingen.TileScheme.covering(grid, cells=128)

    finest = scheme.level_for(1.0)
    whole = terraingen.evaluate(terrain, grid=grid).heights.values
    keys = scheme.tiles_covering(grid.bounds, finest)
    tiles = terraingen.evaluate_tiles(terrain, scheme, keys)
    same = all(
        np.array_equal(
            tile.heights.values,
            whole[
                int(round(grid.transform.origin_y - scheme.grid(key).transform.origin_y)) :][:128,
                int(round(scheme.grid(key).transform.origin_x - grid.transform.origin_x)) :][:, :128],
        )
        for key, tile in zip(keys, tiles)
    )
    print(f"level {finest}: {len(keys)} tiles of {scheme.tile_size(finest):.0f} m, equal to the whole grid: {same}")

    shades = []
    for level in (finest - 2, finest - 1, finest):
        key = scheme.tile_at(-1.0, 1.0, level)
        tile = terraingen.evaluate_tile(terrain, scheme, key)
        print(f"level {level}: tile {key}, {scheme.spacing(level):.0f} m spacing, {len(tile.craters['x'])} craters")
        shades.append(hillshade(tile.heights.values, scheme.spacing(level)))
    print("wrote", write_png(out / "example_tiles_quadtree.png", montage(shades, columns=3)))


if __name__ == "__main__":
    main()
