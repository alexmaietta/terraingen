"""A synthetic terrain built in code: a flat source with one fBm noise layer.

Evaluates it over the source's grid, queries single points as a physics engine would, and writes
a GeoTIFF and a hillshade. The same terrain as examples/config/terrain/example_terrain_fbm.json.

    python example_terrain_fbm.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, write_png


class Example(_common.Example):
    def generate(self):
        fbm = terraingen.NoiseLayer(
            terraingen.FractalParams(seed=1, wavelength=800.0, amplitude=40.0, octaves=8)
        )
        terrain = terraingen.Terrain(terraingen.FlatSource(513, 513, 2.0), [terraingen.NoiseRelief([fbm])])

        self.heights = terraingen.evaluate(terrain)
        heights = self.heights.values
        print(f"heights from {heights.min():.2f} to {heights.max():.2f} m")
        self.points = []
        for x, y in ((0.0, 0.0), (120.5, -310.25)):
            self.points.append(terraingen.evaluate_at(terrain, x, y, 2.0))
            print(f"height at ({x}, {y}): {self.points[-1]:.3f} m")
        self.show("heights", self.heights)

    def save(self, out):
        terraingen.write_geotiff(out / "example_terrain_fbm.tif", self.heights)
        print(
            "wrote",
            write_png(
                out / "example_terrain_fbm.png",
                hillshade(self.heights.values, 2.0, exaggeration=3.0),
            ),
        )

    def test_final(self):
        heights = self.heights.values
        assert heights.shape == (513, 513), "the heights are 513 x 513"
        assert np.all(np.isfinite(heights)), "every height is finite"
        assert 10.0 < heights.max() - heights.min() < 400.0, (
            "the relief is tens of metres, as a 40 m amplitude gives"
        )
        assert all(np.isfinite(h) and heights.min() - 1.0 <= h <= heights.max() + 1.0 for h in self.points), (
            "the point queries lie within the grid's height range"
        )


if __name__ == "__main__":
    _common.main(Example)
