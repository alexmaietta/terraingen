# SPDX-License-Identifier: BSD-3-Clause

"""Irregular and elongated crater outlines.

Real craters are not perfect circles: `CraterIrregularity` roughens rims with random harmonics (a
power-law spectrum or the fits of Du et al. 2024) and stretches a share of craters into ellipses,
optionally along a common heading. Writes a montage: round craters, rough rims, elongated craters
and both together.

    python example_craters_irregularity.py [--viewer window|null] [--output-dir DIR] [--test]
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
            terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
            terraingen.FassettThomsonProfile(),
        )
        uniform = terraingen.ValueDistribution.Kind.UNIFORM
        variants = {
            "round": terraingen.CraterIrregularity(),
            "rough rims": terraingen.CraterIrregularity(
                enabled=True,
                seed=3,
                elongated_share=0.0,
                roughness=terraingen.ValueDistribution(uniform, 0.03, 0.06),
            ),
            "elongated": terraingen.CraterIrregularity(
                enabled=True,
                seed=3,
                roughness=terraingen.ValueDistribution(a=0.0),
                elongated_share=0.6,
                ellipticity=terraingen.ValueDistribution(uniform, 1.3, 1.8),
                heading_deg=30.0,
                heading_spread_deg=10.0,
            ),
            "both": terraingen.CraterIrregularity(
                enabled=True,
                seed=3,
                roughness=terraingen.ValueDistribution(uniform, 0.02, 0.05),
                elongated_share=0.3,
            ),
        }
        generation = terraingen.CraterGenerationOptions(d_min=15.0, d_max=150.0)
        self.heights = {}
        for name, irregularity in variants.items():
            apply = terraingen.CraterApplyOptions(irregularity=irregularity)
            layer = terraingen.CraterLayer(
                models, terraingen.CraterLayerOptions(seed=4, generation=generation, apply=apply)
            )
            heights = terraingen.evaluate(
                terraingen.TerrainGenerator(terraingen.FlatSource(257, 257, PIXEL), [layer])
            )
            print(f"{name:12s} done")
            self.heights[name] = heights.values
            self.show(name, heights)

    def save(self, out):
        shades = [hillshade(heights, PIXEL) for heights in self.heights.values()]
        print("wrote", write_png(out / "example_craters_irregularity.png", montage(shades, columns=4)))

    def test_final(self):
        assert len(self.heights) == 4, "four outline variants"
        assert all(
            heights.shape == (257, 257) and np.all(np.isfinite(heights)) for heights in self.heights.values()
        ), "every terrain is 257 x 257 and finite"
        round_heights = self.heights["round"]
        assert round_heights.max() - round_heights.min() > 1.0, "the round craters have relief"
        assert all(
            np.abs(heights - round_heights).max() > 0.1
            for name, heights in self.heights.items()
            if name != "round"
        ), "every irregular variant changes the round craters"


if __name__ == "__main__":
    _common.main(Example)
