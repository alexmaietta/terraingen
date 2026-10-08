"""Lunar craters on a synthetic terrain.

A `CraterLayer` with the DSNE lunar surface model for a 3.5 Gyr surface (crater density and ages),
freshness degradation and the Fassett and Thomson profile, added to the fBm terrain of
example_terrain_fbm.py. Prints the cumulative counts by size and writes the heights, the crater
list and a hillshade. The same terrain as examples/config/craters/example_craters_simple.json.

    python example_craters_simple.py [OUTPUT_DIR]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, output_dir, write_png


def main():
    out = output_dir()
    fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=800.0, amplitude=40.0))
    surface = terraingen.lunar_surface(3.5)
    models = terraingen.CraterModels(
        density=surface.density,
        ages=surface.ages,
        degradation=terraingen.FreshnessDegradation(surface.ages),
        profile=terraingen.FassettThomsonProfile(),
    )
    craters = terraingen.CraterLayer(
        models, terraingen.CraterLayerOptions(seed=1, generation=terraingen.CraterGenerationOptions(min_diameter=8.0, max_diameter=300.0))
    )
    terrain = terraingen.Terrain(terraingen.FlatSource(513, 513, 2.0), [terraingen.NoiseRelief([fbm]), craters])

    evaluated = terraingen.evaluate(terrain)
    diameters = evaluated.craters["diameter"]
    print(f"{len(diameters)} craters from {diameters.min():.1f} to {diameters.max():.1f} m")
    for d in (8, 16, 32, 64, 128):
        print(f"  N(>= {d:3d} m) = {np.count_nonzero(diameters >= d)}")

    terraingen.write_geotiff(out / "example_craters_simple.tif", evaluated.heights)
    evaluated.write_crater_list(out / "example_craters_simple.csv")
    print("wrote", write_png(out / "example_craters_simple.png", hillshade(evaluated.heights.values, 2.0)))


if __name__ == "__main__":
    main()
