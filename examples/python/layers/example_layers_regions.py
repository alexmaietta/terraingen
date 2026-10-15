"""Regions: different layers in different parts of the terrain.

`RegionLayer` applies a layer only inside a rectangle or circle, fading it out over `feather`
metres outside. Here a circular mare is flattened and lowered (a `ScaleLayer` in a circle), the
west half is an old, heavily cratered surface and the east half a younger one. Prints each region's
crater count and writes the heights and a hillshade.

    python example_layers_regions.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, write_png

PIXEL = 4.0
HALF = 513 * PIXEL / 2


def craters(age_gyr, seed):
    surface = terraingen.lunar_surface(age_gyr)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    generation = terraingen.CraterGenerationOptions(min_diameter=10.0, max_diameter=200.0)
    return terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=seed, generation=generation))


class Example(_common.Example):
    def generate(self):
        relief = terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=9, wavelength=1200.0, amplitude=25.0, octaves=6))])
        self.mare = terraingen.RegionLayer(terraingen.ScaleLayer(0.2, -15.0), terraingen.RegionShape.circle(0.0, 0.0, 600.0), feather=150.0)
        west = terraingen.RegionLayer(craters(3.9, 9), terraingen.RegionShape.rectangle(-HALF, -HALF, 0.0, HALF), feather=200.0)
        east = terraingen.RegionLayer(craters(1.0, 10), terraingen.RegionShape.rectangle(0.0, -HALF, HALF, HALF), feather=200.0)
        terrain = terraingen.Terrain(terraingen.FlatSource(513, 513, PIXEL), [relief, self.mare, west, east])
        self.evaluated = terraingen.evaluate(terrain)

        x = self.evaluated.craters["x"]
        print(f"{np.count_nonzero(x < 0)} craters in the west, {np.count_nonzero(x >= 0)} in the east")
        print(f"mare weight at the centre {self.mare.weight(0.0, 0.0):.2f}, 700 m out {self.mare.weight(700.0, 0.0):.2f}")
        self.show("heights", self.evaluated.heights)

    def save(self, out):
        terraingen.write_geotiff(out / "example_layers_regions.tif", self.evaluated.heights)
        print("wrote", write_png(out / "example_layers_regions.png", hillshade(self.evaluated.heights.values, PIXEL)))

    def test_final(self):
        heights = self.evaluated.heights.values
        x = self.evaluated.craters["x"]
        age = self.evaluated.craters["age_gyr"]
        assert heights.shape == (513, 513) and np.isfinite(heights).all(), "finite 513 x 513 heights"
        assert len(x) > 1000, "more than 1000 craters"
        assert self.mare.weight(0.0, 0.0) > 0.99 and 0.0 < self.mare.weight(700.0, 0.0) < 0.5 and self.mare.weight(800.0, 0.0) < 1e-6, "the mare fades out over its 150 m feather"
        assert 0.0 < age[x > 200.0].max() <= 1.0 + 1e-6, "craters east of the feather are no older than the 1 Gyr east half"
        assert age[x < -200.0].max() > 2.0, "the 3.9 Gyr west half has craters older than 2 Gyr"


if __name__ == "__main__":
    _common.main(Example)
