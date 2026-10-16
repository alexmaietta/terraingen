"""Rim smoothing: soften the sharp crests a crater profile leaves.

`RimSmoothingFilter` blurs each crater's rim with a Gaussian of `sigma_radii` crater radii, over a
band from `inner_radii` to `outer_radii` that fades along `shape`, at `strength` from 0 to 1. It is a
filter, so it runs after every layer, smoothing the craters of the layers it is given. Prints the highest rim in
each case and writes a montage of hillshades.

    python example_craters_rim_smoothing.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, montage, write_png

PIXEL = 1.0


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
            terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
            terraingen.FassettThomsonProfile(),
        )
        generation = terraingen.CraterGenerationOptions(min_diameter=10.0, max_diameter=120.0)
        craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=3, generation=generation))
        source = terraingen.FlatSource(257, 257, PIXEL)
        variants = {
            "none": None,
            "default": terraingen.RimSmoothing(),
            "wide": terraingen.RimSmoothing(sigma_radii=0.15, inner_radii=0.6, outer_radii=1.4),
            "half strength": terraingen.RimSmoothing(sigma_radii=0.15, strength=0.5),
        }
        self.heights = {}
        for name, options in variants.items():
            filters = [terraingen.RimSmoothingFilter(options, [craters])] if options else []
            heights = terraingen.evaluate(terraingen.Terrain(source, [craters], filters)).values
            print(f"{name:14s} highest rim {heights.max():.2f} m")
            self.heights[name] = heights
        for name in ("default", "none", "wide", "half strength"):
            self.show(name, self.heights[name], PIXEL)

    def save(self, out):
        shades = [hillshade(heights, PIXEL) for heights in self.heights.values()]
        print("wrote", write_png(out / "example_craters_rim_smoothing.png", montage(shades, columns=4)))

    def test_final(self):
        highest = {name: heights.max() for name, heights in self.heights.items()}
        assert all(
            heights.shape == (257, 257) and np.all(np.isfinite(heights)) for heights in self.heights.values()
        ), "every terrain is 257 x 257 and finite"
        assert highest["none"] > 0.0, "the unsmoothed rims stand above the datum"
        assert all(
            highest[name] <= highest["none"] + 1e-3 for name in ("default", "wide", "half strength")
        ), "smoothing never raises the highest rim"
        assert np.abs(self.heights["default"] - self.heights["none"]).max() > 0.01, (
            "the default smoothing changes the rims"
        )


if __name__ == "__main__":
    _common.main(Example)
