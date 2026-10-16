"""The same cratered terrain at several surface ages.

`lunar_surface` is the Neukum production for the surface's age, capped at Trask's equilibrium:
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
        self.evaluated = []
        for age in AGES:
            surface = terraingen.lunar_surface(age)
            models = terraingen.CraterModels(
                surface.density,
                surface.ages,
                terraingen.FreshnessDegradation(surface.ages),
                terraingen.FassettThomsonProfile(),
            )
            craters = terraingen.CraterLayer(
                models,
                terraingen.CraterLayerOptions(
                    seed=21,
                    generation=terraingen.CraterGenerationOptions(min_diameter=20.0, max_diameter=200.0),
                ),
            )
            terrain = terraingen.Terrain(
                terraingen.FlatSource(257, 257, PIXEL), [terraingen.NoiseRelief([fbm]), craters]
            )
            evaluated = terraingen.evaluate(terrain)
            print(f"{age:4.2f} Gyr: {len(evaluated.craters['x']):3d} craters of 20 m or more")
            self.evaluated.append(evaluated)
            self.show(f"{age:g} Gyr", evaluated.heights)

    def save(self, out):
        shades = [hillshade(evaluated.heights.values, PIXEL) for evaluated in self.evaluated]
        print("wrote", write_png(out / "example_craters_age_sweep.png", montage(shades, columns=4)))

    def test_final(self):
        counts = [len(evaluated.craters["x"]) for evaluated in self.evaluated]
        assert len(counts) == len(AGES), "one terrain per age"
        assert all(
            evaluated.heights.shape == (257, 257) and np.all(np.isfinite(evaluated.heights.values))
            for evaluated in self.evaluated
        ), "every terrain is 257 x 257 and finite"
        assert all(a <= b for a, b in zip(counts, counts[1:], strict=False)), (
            "older surfaces have at least as many craters"
        )
        assert counts[-1] > 2 * max(counts[0], 1), "the oldest surface has far more craters than the youngest"
        assert all(
            evaluated.craters["diameter"].min() >= 20.0
            for evaluated in self.evaluated
            if len(evaluated.craters["diameter"])
        ), "every crater is 20 m or more"


if __name__ == "__main__":
    _common.main(Example)
