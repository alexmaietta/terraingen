"""The same cratered terrain at several surface ages.

Each surface is the Neukum production for its age, capped at Trask's equilibrium by a `MinimumSfd`:
young surfaces gain craters quickly, and craters of tens of metres saturate within about a billion
years, after which new craters erase as many as they add. The seed stays fixed, so each panel
differs only by age. Prints counts and writes a montage of hillshades.

    python example_craters_age_sweep.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, montage, write_png

PIXEL = 2.0
AGES = (0.01, 0.05, 0.3, 3.9)


class Example(_common.Example):
    def generate(self):
        fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=400.0, amplitude=10.0))
        self.heights, self.diameters = [], []
        for age in AGES:
            production = terraingen.extended_neukum_production_function()
            equilibrium = terraingen.trask_equilibrium()
            density = terraingen.MinimumSfd(
                [
                    terraingen.ProductionSurface(production, age, terraingen.neukum_lunar_chronology),
                    equilibrium,
                ]
            )
            ages = terraingen.SteadyStateAges(
                production, equilibrium, age, terraingen.neukum_lunar_chronology
            )
            models = terraingen.CraterModels(
                density,
                ages,
                terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
                terraingen.FassettThomsonProfile(),
            )
            craters = terraingen.CraterLayer(
                models,
                terraingen.CraterLayerOptions(
                    seed=21,
                    generation=terraingen.CraterGenerationOptions(d_min=20.0, d_max=200.0),
                ),
            )
            terrain = terraingen.TerrainGenerator(
                terraingen.FlatSource(257, 257, PIXEL), [terraingen.NoiseRelief([fbm]), craters]
            )
            heights = terraingen.evaluate(terrain)
            diameters = terraingen.placed_craters(terrain).table["diameter"]
            print(f"{age:4.2f} Gyr: {len(diameters):3d} craters of 20 m or more")
            self.heights.append(heights)
            self.diameters.append(diameters)
            self.show(f"{age:g} Gyr", heights)

    def save(self, out):
        shades = [hillshade(heights.values, PIXEL) for heights in self.heights]
        print("wrote", write_png(out / "example_craters_age_sweep.png", montage(shades, columns=4)))

    def test_final(self):
        counts = [len(diameters) for diameters in self.diameters]
        assert len(counts) == len(AGES), "one terrain per age"
        assert all(
            heights.shape == (257, 257) and np.all(np.isfinite(heights.values)) for heights in self.heights
        ), "every terrain is 257 x 257 and finite"
        assert all(a <= b for a, b in zip(counts, counts[1:], strict=False)), (
            "older surfaces have at least as many craters"
        )
        assert counts[-1] > 2 * max(counts[0], 1), "the oldest surface has far more craters than the youngest"
        assert all(diameters.min() >= 20.0 for diameters in self.diameters if len(diameters)), (
            "every crater is 20 m or more"
        )


if __name__ == "__main__":
    _common.main(Example)
