# SPDX-License-Identifier: BSD-3-Clause

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
        production = terraingen.extended_neukum_production_function()
        equilibrium = terraingen.trask_equilibrium()
        density = terraingen.MinimumSfd(
            [terraingen.ProductionSurface(production, 3.5, terraingen.neukum_lunar_chronology), equilibrium]
        )
        ages = terraingen.SteadyStateAges(production, equilibrium, 3.5, terraingen.neukum_lunar_chronology)
        models = terraingen.CraterModels(
            density,
            ages,
            terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
            terraingen.FassettThomsonProfile(),
        )
        craters = terraingen.CraterLayer(
            models,
            terraingen.CraterLayerOptions(
                generation=terraingen.CraterGenerationOptions(d_min=3.0, d_max=20.0)
            ),
        )
        area = (SIZE * PIXEL) ** 2

        self.heights, self.boulders = {}, {}
        for coverage in (0.02, 0.05, 0.10):
            options = terraingen.BoulderLayerOptions(
                distribution=None,
                coverage=coverage,
                min_diameter_m=0.2,
                max_diameter_m=3.0,
                model_threshold=terraingen.BoulderModelThreshold(min_diameter_m=0.6),
            )
            boulders = terraingen.BoulderLayer(options, craters)
            terrain = terraingen.TerrainGenerator(
                terraingen.FlatSource(SIZE, SIZE, PIXEL), [terraingen.NoiseRelief([fbm]), craters, boulders]
            )
            self.heights[coverage] = terraingen.evaluate(terrain)
            self.boulders[coverage] = terraingen.placed_boulders(terrain)
            diameters = self.boulders[coverage].table["diameter"]
            counts = ", ".join(
                f"N(>= {d}) = {np.count_nonzero(diameters >= d) / area:.3f}" for d in (0.2, 0.5, 1.0)
            )
            print(
                f"coverage {coverage:.2f}: {len(diameters)} boulders, {len(self.boulders[coverage].models)} boulder models; per m^2 {counts}"
            )
        for coverage in (0.05, 0.02, 0.10):
            self.show(f"coverage {coverage:.2f}", self.heights[coverage])

    def save(self, out):
        heights, boulders = self.heights[0.05], self.boulders[0.05]
        terraingen.write_geotiff(out / "example_boulders_layer.tif", heights)
        boulders.write_list(out / "example_boulders_layer.csv")
        boulders.write_models(out / "example_boulders_layer.glb")
        print("wrote", write_png(out / "example_boulders_layer.png", hillshade(heights.values, PIXEL)))

    def test_final(self):
        counts = [len(self.boulders[c].boulders) for c in (0.02, 0.05, 0.10)]
        models = [len(self.boulders[c].models) for c in (0.02, 0.05, 0.10)]
        assert all(h.shape == (SIZE, SIZE) for h in self.heights.values()), "the heights are 401 x 401"
        assert 0 < counts[0] < counts[1] < counts[2], "more coverage places more boulders"
        assert models[0] <= models[1] <= models[2] and models[2] > 0, (
            "more coverage places more boulder models"
        )
        for placed in self.boulders.values():
            diameters = placed.table["diameter"]
            assert diameters.min() >= 0.2 and diameters.max() <= 3.0, "every boulder is 0.2 to 3 m across"
            assert len(placed.models) < len(diameters), "only some boulders are boulder models"


if __name__ == "__main__":
    _common.main(Example)
