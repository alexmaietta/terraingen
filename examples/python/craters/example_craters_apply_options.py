# SPDX-License-Identifier: BSD-3-Clause

"""How craters are applied to rough terrain: datum, interior replacement and blend.

By default a crater's profile is added to the terrain, so its floor follows the rough terrain
beneath. A `datum` instead cuts the interior relative to the ground fitted under the crater: a
plane, its mean height or the height at its center. With a datum, `interior_replacement` sets how
much of the interior replaces the older relief (1 erases it, as an impact excavates it; 0 keeps it)
and `blend` where the replaced interior gives way to the draped rim. Writes a montage of hillshades
of the same craters on the same rough terrain.

    python example_craters_apply_options.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, montage, write_png

PIXEL = 2.0


class Example(_common.Example):
    def generate(self):
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
        rough = terraingen.NoiseRelief(
            [terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=150.0, amplitude=25.0))]
        )
        options = {
            "default": terraingen.CraterApplyOptions(),
            "plane datum": terraingen.CraterApplyOptions(datum=terraingen.CraterDatum.PLANE),
            "mean-height datum": terraingen.CraterApplyOptions(datum=terraingen.CraterDatum.MEAN_HEIGHT),
            "center-height datum": terraingen.CraterApplyOptions(datum=terraingen.CraterDatum.CENTER_HEIGHT),
            "plane, half replaced": terraingen.CraterApplyOptions(
                datum=terraingen.CraterDatum.PLANE, interior_replacement=0.5
            ),
            "plane, wide blend": terraingen.CraterApplyOptions(
                datum=terraingen.CraterDatum.PLANE,
                blend=terraingen.CraterBlend(
                    start_radii=0.5, end_radii=1.5, shape=terraingen.BlendShape.SMOOTHSTEP
                ),
            ),
        }
        generation = terraingen.CraterGenerationOptions(d_min=20.0, d_max=300.0)
        self.heights = {}
        for name, apply in options.items():
            layer = terraingen.CraterLayer(
                models, terraingen.CraterLayerOptions(seed=1, generation=generation, apply=apply)
            )
            heights = terraingen.evaluate(
                terraingen.TerrainGenerator(terraingen.FlatSource(257, 257, PIXEL), [rough, layer])
            ).values
            print(f"{name:20s} heights {heights.min():7.1f} to {heights.max():6.1f} m")
            self.heights[name] = heights
            self.show(name, heights, PIXEL)

    def save(self, out):
        shades = [hillshade(heights, PIXEL) for heights in self.heights.values()]
        print("wrote", write_png(out / "example_craters_apply_options.png", montage(shades, columns=3)))

    def test_final(self):
        assert len(self.heights) == 6, "six ways of applying the craters"
        assert all(
            heights.shape == (257, 257) and np.all(np.isfinite(heights)) for heights in self.heights.values()
        ), "every terrain is 257 x 257 and finite"
        default = self.heights["default"]
        assert all(
            np.abs(heights - default).max() > 0.1
            for name, heights in self.heights.items()
            if name != "default"
        ), "every datum option changes the default"
        plane, half = self.heights["plane datum"], self.heights["plane, half replaced"]
        assert np.abs(half - default).mean() < np.abs(plane - default).mean(), (
            "replacing half the interior stays closer to the default than replacing all of it"
        )


if __name__ == "__main__":
    _common.main(Example)
