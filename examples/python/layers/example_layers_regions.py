"""Regions: different layers in different parts of the terrain.

`RegionLayer` applies a layer only inside a rectangle or circle, fading it out over `feather`
metres outside. Here a circular mare is flattened and lowered (a `ScaleLayer` in a circle), the
west half is an old, heavily cratered surface and the east half a younger one. Prints each region's
crater count and writes the heights and a hillshade.

    python example_layers_regions.py [OUTPUT_DIR]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, output_dir, write_png

PIXEL = 4.0
HALF = 513 * PIXEL / 2


def craters(age_gyr, seed):
    surface = terraingen.lunar_surface(age_gyr)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    generation = terraingen.CraterGenerationOptions(min_diameter=10.0, max_diameter=200.0)
    return terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=seed, generation=generation))


def main():
    out = output_dir()
    relief = terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=9, wavelength=1200.0, amplitude=25.0, octaves=6))])
    mare = terraingen.RegionLayer(terraingen.ScaleLayer(0.2, -15.0), terraingen.RegionShape.circle(0.0, 0.0, 600.0), feather=150.0)
    west = terraingen.RegionLayer(craters(3.9, 9), terraingen.RegionShape.rectangle(-HALF, -HALF, 0.0, HALF), feather=200.0)
    east = terraingen.RegionLayer(craters(1.0, 10), terraingen.RegionShape.rectangle(0.0, -HALF, HALF, HALF), feather=200.0)
    terrain = terraingen.Terrain(terraingen.FlatSource(513, 513, PIXEL), [relief, mare, west, east])
    evaluated = terraingen.evaluate(terrain)

    x = evaluated.craters["x"]
    print(f"{np.count_nonzero(x < 0)} craters in the west, {np.count_nonzero(x >= 0)} in the east")
    print(f"mare weight at the centre {mare.weight(0.0, 0.0):.2f}, 700 m out {mare.weight(700.0, 0.0):.2f}")
    terraingen.write_geotiff(out / "example_layers_regions.tif", evaluated.heights)
    print("wrote", write_png(out / "example_layers_regions.png", hillshade(evaluated.heights.values, PIXEL)))


if __name__ == "__main__":
    main()
