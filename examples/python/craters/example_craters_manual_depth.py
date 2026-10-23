"""Hand-placed craters given an explicit rim-to-floor depth, one with its rim height as well.

The craters lie on a small flat site under the Martin-Parkes-Dunstan profile, which honours both;
one more crater gives only its diameter and takes its depth from the degradation model. The same
terrain as examples/config/craters/example_craters_manual_depth.json.

    python example_craters_manual_depth.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import math
import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, write_png

PIXEL = 0.25
SIZE = 401


class Example(_common.Example):
    def generate(self):
        ages = terraingen.UniformAges(1.0)
        models = terraingen.CraterModels(
            density=terraingen.PowerLawSfd(1e-6, 2.0),
            ages=ages,
            degradation=terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
            profile=terraingen.MartinParkesDunstanProfile(),
        )
        manual = [
            terraingen.Crater(-22.0, 20.0, 20.0, depth=3.0, rim_height=0.8),
            terraingen.Crater(22.0, 22.0, 16.0, depth=1.2),
            terraingen.Crater(-20.0, -22.0, 24.0, depth=2.0, rim_height=0.3),
            terraingen.Crater(22.0, -20.0, 18.0),
        ]
        craters = terraingen.CraterLayer(
            models,
            terraingen.CraterLayerOptions(
                generate_random=False, generation=terraingen.CraterGenerationOptions(manual=manual)
            ),
        )
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(SIZE, SIZE, PIXEL), [craters])

        self.heights = terraingen.evaluate(terrain)
        placed = terraingen.placed_craters(terrain)
        self.asked = placed.craters
        self.craters = placed.table
        for diameter, depth, rim, floor_depth, rim_height in zip(
            self.craters["diameter"],
            self.asked.depths,
            self.asked.rim_heights,
            self.craters["floor_depth"],
            self.craters["rim_height"],
            strict=True,
        ):
            asked = "depth from the model" if math.isnan(depth) else f"asked {depth:.1f} m deep"
            if not math.isnan(rim):
                asked += f" with a {rim:.1f} m rim"
            print(
                f"{diameter:2.0f} m crater, {asked}: floor {floor_depth:.2f} m below and rim "
                f"{rim_height:.2f} m above the surface, rim to floor {floor_depth + rim_height:.2f} m"
            )
        self.show("heights", self.heights)

    def save(self, out):
        terraingen.write_geotiff(out / "example_craters_manual_depth.tif", self.heights)
        print(
            "wrote",
            write_png(out / "example_craters_manual_depth.png", hillshade(self.heights.values, PIXEL)),
        )

    def test_final(self):
        table = self.craters
        depths = np.asarray(self.asked.depths)
        rims = np.asarray(self.asked.rim_heights)
        total = table["floor_depth"] + table["rim_height"]
        tolerance = 1e-3 * table["diameter"]
        assert self.heights.shape == (SIZE, SIZE), "the heights are 401 x 401"
        assert np.all(np.isfinite(self.heights.values)), "every height is finite"
        assert sorted(table["diameter"]) == [16.0, 18.0, 20.0, 24.0], "exactly the four hand-placed craters"
        given = ~np.isnan(depths)
        assert np.all(np.abs(total - depths)[given] < tolerance[given]), (
            "each crater given a depth is that deep from rim to floor"
        )
        rimmed = ~np.isnan(rims)
        assert np.count_nonzero(rimmed) == 2, "two craters give a rim height"
        assert np.all(np.abs(table["rim_height"] - rims)[rimmed] < tolerance[rimmed]), (
            "each crater given a rim height has that rim"
        )
        assert np.all(total[~given] > 1.0), "the crater given only a diameter takes its depth from the model"
        assert self.heights.values.min() < -1.0, "the craters are cut into the site"


if __name__ == "__main__":
    _common.main(Example)
