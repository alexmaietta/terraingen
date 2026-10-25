# SPDX-License-Identifier: BSD-3-Clause

"""Boulders where bedrock is exposed: crests and steep slopes shed regolith and show boulder.

An `ExposureField` estimates, from the ground beneath, how exposed each point is: 0 under a full
regolith cover, 1 on bare crests and steep walls. With `BoulderBedrock.exposure` set, a boulder layer
places up to `BoulderBedrock.abundance` times more boulders where the ground is exposed. This example writes
the exposure map of a rough terrain and compares boulder counts on exposed and covered ground.

    python example_boulders_exposure.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, montage, write_png

PIXEL = 0.25
SIZE = 401


class Example(_common.Example):
    def generate(self):
        relief = terraingen.NoiseRelief(
            [
                terraingen.NoiseLayer(
                    terraingen.FractalParams(seed=1, wavelength=60.0, amplitude=6.0, octaves=7)
                )
            ]
        )
        exposure = terraingen.ExposureParams(reach=6.0)
        grid = terraingen.RasterGrid.centered((0, 0), (SIZE * PIXEL, SIZE * PIXEL), PIXEL)
        ground = terraingen.TerrainGenerator(terraingen.FlatSource(SIZE, SIZE, PIXEL), [relief])
        self.exposure_map = terraingen.ExposureField(exposure).fill(ground, 1, grid, PIXEL).values

        boulders = terraingen.BoulderLayer(
            terraingen.BoulderLayerOptions(
                seed=1,
                sizes=terraingen.BoulderSizes(
                    distribution=None, coverage=0.02, min_diameter=0.3, max_diameter=4.0
                ),
                bedrock=terraingen.BoulderBedrock(exposure=exposure, abundance=4.0),
            )
        )
        terrain = terraingen.TerrainGenerator(ground.source, [relief, boulders])
        self.heights = terraingen.evaluate(terrain)
        table = terraingen.placed_boulders(terrain).table
        cols = np.clip(((table["x"] - grid.transform.origin_x) / PIXEL).astype(int), 0, SIZE - 1)
        rows = np.clip(((grid.transform.origin_y - table["y"]) / PIXEL).astype(int), 0, SIZE - 1)
        self.exposed_here = self.exposure_map[rows, cols] > 0.5
        self.share = np.mean(self.exposure_map > 0.5)
        print(
            f"{self.share:.0%} of the ground is more than half exposed; it holds {self.exposed_here.mean():.0%} of the {len(cols)} boulders"
        )
        self.show("heights", self.heights)

    def save(self, out):
        images = [self.exposure_map, hillshade(self.heights.values, PIXEL)]
        print("wrote", write_png(out / "example_boulders_exposure.png", montage(images, columns=2)))

    def test_final(self):
        assert self.heights.shape == (SIZE, SIZE)
        assert self.exposure_map.shape == (SIZE, SIZE), "the exposure map is on the heights' grid"
        assert np.all((self.exposure_map >= 0.0) & (self.exposure_map <= 1.0)), "exposure lies in [0, 1]"
        assert 0.05 < self.share < 0.95, "some, not all, of the ground is exposed"
        assert len(self.exposed_here) > 20, "more than 20 boulders"
        assert self.exposed_here.mean() > self.share + 0.1, (
            "exposed ground holds more than its share of boulders"
        )


if __name__ == "__main__":
    _common.main(Example)
