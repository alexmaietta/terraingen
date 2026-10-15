"""Lunar craters on a synthetic terrain.

A `CraterLayer` with the DSNE lunar surface model for a 3.5 Gyr surface (crater density and ages),
freshness degradation and the Fassett and Thomson profile, added to the fBm terrain of
example_terrain_fbm.py. Prints the cumulative counts by size and writes the heights, the crater
list and a hillshade. The same terrain as examples/config/craters/example_craters_simple.json.

    python example_craters_simple.py [--viewer window|null] [--output-dir DIR] [--test]
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

        self.evaluated = terraingen.evaluate(terrain)
        diameters = self.evaluated.craters["diameter"]
        print(f"{len(diameters)} craters from {diameters.min():.1f} to {diameters.max():.1f} m")
        for d in (8, 16, 32, 64, 128):
            print(f"  N(>= {d:3d} m) = {np.count_nonzero(diameters >= d)}")
        self.show("heights", self.evaluated.heights)

    def save(self, out):
        terraingen.write_geotiff(out / "example_craters_simple.tif", self.evaluated.heights)
        self.evaluated.write_crater_list(out / "example_craters_simple.csv")
        print("wrote", write_png(out / "example_craters_simple.png", hillshade(self.evaluated.heights.values, 2.0)))

    def test_final(self):
        diameters = self.evaluated.craters["diameter"]
        assert self.evaluated.heights.shape == (513, 513)
        assert len(diameters) > 100, "more than 100 craters"
        assert diameters.min() >= 8.0 and diameters.max() <= 300.0, "every crater is 8 to 300 m across"
        assert 2 * np.count_nonzero(diameters < 16.0) > len(diameters), "most craters are under 16 m"


if __name__ == "__main__":
    _common.main(Example)
