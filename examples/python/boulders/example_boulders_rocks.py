"""Rocks on a cratered synthetic terrain, small ones in the heights and large ones as 3D boulders.

`RockLayer` draws rocks from Golombek and Rapp's (1997) model, in which the fractional area
covered by rocks at least D across is k exp(-q(k) D) for a coverage k. This example compares three
coverages, then writes the heights, the rock list and the boulders (rocks of 0.6 m or more) as a
glTF scene for the middle one. Like examples/config/boulders/example_boulders_models.json, with craters.

    python example_boulders_rocks.py [OUTPUT_DIR]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, output_dir, write_png

PIXEL = 0.1
SIZE = 401


def main():
    out = output_dir()
    fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=40.0, amplitude=0.5, octaves=6))
    surface = terraingen.lunar_surface(3.5)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    craters = terraingen.CraterLayer(
        models, terraingen.CraterLayerOptions(generation=terraingen.CraterGenerationOptions(min_diameter=3.0, max_diameter=20.0))
    )
    area = (SIZE * PIXEL) ** 2

    for coverage in (0.02, 0.05, 0.10):
        options = terraingen.RockLayerOptions(
            coverage=coverage, min_diameter=0.2, max_diameter=3.0, models=terraingen.RockModels(min_diameter=0.6)
        )
        rocks = terraingen.RockLayer(options, craters)
        terrain = terraingen.Terrain(
            terraingen.FlatSource(SIZE, SIZE, PIXEL), [terraingen.NoiseRelief([fbm]), craters, rocks]
        )
        evaluated = terraingen.evaluate(terrain)
        diameters = evaluated.rocks["diameter"]
        counts = ", ".join(f"N(>= {d}) = {np.count_nonzero(diameters >= d) / area:.3f}" for d in (0.2, 0.5, 1.0))
        print(f"coverage {coverage:.2f}: {len(diameters)} rocks, {evaluated.boulder_count} boulders; per m^2 {counts}")
        if coverage == 0.05:
            terraingen.write_geotiff(out / "example_boulders_rocks.tif", evaluated.heights)
            evaluated.write_rock_list(out / "example_boulders_rocks.csv")
            evaluated.write_boulder_models(out / "example_boulders_rocks.glb")
            print("wrote", write_png(out / "example_boulders_rocks.png", hillshade(evaluated.heights.values, PIXEL)))


if __name__ == "__main__":
    main()
