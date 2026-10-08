"""Rocks where bedrock is exposed: crests and steep slopes shed regolith and show rock.

An `ExposureField` estimates, from the ground beneath, how exposed each point is: 0 under a full
regolith cover, 1 on bare crests and steep walls. With `RockLayerOptions.exposure` set, a rock layer
places up to `bedrock_abundance` times more rocks where the ground is exposed. This example writes
the exposure map of a rough terrain and compares rock counts on exposed and covered ground.

    python example_boulders_exposure.py [OUTPUT_DIR]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, montage, output_dir, write_png

PIXEL = 0.25
SIZE = 401


def main():
    out = output_dir()
    relief = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=60.0, amplitude=6.0, octaves=7))]
    )
    exposure = terraingen.ExposureParams(reach=6.0)
    grid = terraingen.RasterGrid.centred(SIZE, SIZE, PIXEL)
    ground = terraingen.Terrain(terraingen.FlatSource(SIZE, SIZE, PIXEL), [relief])
    exposure_map = terraingen.ExposureField(exposure).fill(ground, 1, grid, PIXEL).values

    rocks = terraingen.RockLayer(
        terraingen.RockLayerOptions(
            seed=1, coverage=0.02, min_diameter=0.3, max_diameter=4.0, exposure=exposure, bedrock_abundance=4.0
        )
    )
    terrain = terraingen.Terrain(ground.source, [relief, rocks])
    evaluated = terraingen.evaluate(terrain)
    table = evaluated.rocks
    cols = np.clip(((table["x"] - grid.transform.origin_x) / PIXEL).astype(int), 0, SIZE - 1)
    rows = np.clip(((grid.transform.origin_y - table["y"]) / PIXEL).astype(int), 0, SIZE - 1)
    exposed_here = exposure_map[rows, cols] > 0.5
    share = np.mean(exposure_map > 0.5)
    print(f"{share:.0%} of the ground is more than half exposed; it holds {exposed_here.mean():.0%} of the {len(cols)} rocks")
    print("wrote", write_png(out / "example_boulders_exposure.png", montage([exposure_map, hillshade(evaluated.heights.values, PIXEL)], columns=2)))


if __name__ == "__main__":
    main()
