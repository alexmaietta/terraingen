"""Boulders on a cratered synthetic terrain, small ones in the heights and large ones as 3D boulder models.

`BoulderLayer` draws boulders from Golombek and Rapp's (1997) model, in which the fractional area
covered by boulders at least D across is k exp(-q(k) D) for a coverage k. This example compares three
coverages, then writes the heights, the boulder list and the boulders (boulders of 0.6 m or more) as a
glTF scene for the middle one. Like examples/config/boulders/example_boulders_models.json, with craters.

    python example_boulders_layer.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, write_png

PIXEL = 0.1
SIZE = 401


class Example(_common.Example):
    def generate(self):
        fbm = terraingen.NoiseLayer(
            terraingen.FractalParams(seed=1, wavelength=40.0, amplitude=0.5, octaves=6)
        )
        surface = terraingen.lunar_surface(3.5)
        models = terraingen.CraterModels(
            surface.density,
            surface.ages,
            terraingen.FreshnessDegradation(surface.ages),
            terraingen.FassettThomsonProfile(),
        )
        craters = terraingen.CraterLayer(
            models,
            terraingen.CraterLayerOptions(
                generation=terraingen.CraterGenerationOptions(min_diameter=3.0, max_diameter=20.0)
            ),
        )
        area = (SIZE * PIXEL) ** 2

        self.evaluated = {}
        for coverage in (0.02, 0.05, 0.10):
            options = terraingen.BoulderLayerOptions(
                coverage=coverage,
                min_diameter=0.2,
                max_diameter=3.0,
                models=terraingen.BoulderModelThreshold(min_diameter=0.6),
            )
            boulders = terraingen.BoulderLayer(options, craters)
            terrain = terraingen.Terrain(
                terraingen.FlatSource(SIZE, SIZE, PIXEL), [terraingen.NoiseRelief([fbm]), craters, boulders]
            )
            evaluated = terraingen.evaluate(terrain)
            diameters = evaluated.boulders["diameter"]
            counts = ", ".join(
                f"N(>= {d}) = {np.count_nonzero(diameters >= d) / area:.3f}" for d in (0.2, 0.5, 1.0)
            )
            print(
                f"coverage {coverage:.2f}: {len(diameters)} boulders, {evaluated.boulder_model_count} boulders; per m^2 {counts}"
            )
            self.evaluated[coverage] = evaluated
        for coverage in (0.05, 0.02, 0.10):
            self.show(f"coverage {coverage:.2f}", self.evaluated[coverage].heights)

    def save(self, out):
        evaluated = self.evaluated[0.05]
        terraingen.write_geotiff(out / "example_boulders_layer.tif", evaluated.heights)
        evaluated.write_boulder_list(out / "example_boulders_layer.csv")
        evaluated.write_boulder_models(out / "example_boulders_layer.glb")
        print(
            "wrote", write_png(out / "example_boulders_layer.png", hillshade(evaluated.heights.values, PIXEL))
        )

    def test_final(self):
        counts = [len(self.evaluated[c].boulders["diameter"]) for c in (0.02, 0.05, 0.10)]
        boulders = [self.evaluated[c].boulder_model_count for c in (0.02, 0.05, 0.10)]
        assert all(e.heights.shape == (SIZE, SIZE) for e in self.evaluated.values()), (
            "the heights are 401 x 401"
        )
        assert 0 < counts[0] < counts[1] < counts[2], "more coverage places more boulders"
        assert boulders[0] <= boulders[1] <= boulders[2] and boulders[2] > 0, (
            "more coverage places more boulder models"
        )
        for evaluated in self.evaluated.values():
            diameters = evaluated.boulders["diameter"]
            assert diameters.min() >= 0.2 and diameters.max() <= 3.0, "every boulder is 0.2 to 3 m across"
            assert evaluated.boulder_model_count < len(diameters), "only some boulders are boulder models"


if __name__ == "__main__":
    _common.main(Example)
