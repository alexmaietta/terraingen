"""Compare crater degradation models on the same crater population.

`NoDegradation` keeps each crater's fresh depth (or a fixed depth ratio); `FreshnessDegradation`
makes it shallower with its age relative to its lifetime; `DiffusionDegradation` smooths its shape by
topographic diffusion over its age (Fassett and Thomson 2014), with the diffusivity of Fassett and
Thomson (2014) or of Fassett et al. (2022). The craters are the same in every panel. Prints each
model's mean depth ratio and writes a montage of hillshades.

    python example_craters_degradation.py [--viewer window|null] [--output-dir DIR] [--test]
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
        diffused = terraingen.DiffusedProfile(terraingen.FassettThomsonProfile())
        models = {
            "none": terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
            "fixed d/D 0.1": terraingen.NoDegradation(
                terraingen.lunar_fresh_crater_morphometry, depth_ratio=0.1
            ),
            "freshness": terraingen.FreshnessDegradation(
                ages, terraingen.lunar_fresh_crater_morphometry, cutoff_depth_ratio=0.04
            ),
            "diffusion FT2014": terraingen.DiffusionDegradation(
                terraingen.lunar_fresh_crater_morphometry,
                terraingen.fassett_thomson_2014_diffusivity(),
                diffused,
            ),
            "diffusion F2022": terraingen.DiffusionDegradation(
                terraingen.lunar_fresh_crater_morphometry,
                terraingen.fassett_2022_hartmann_diffusivity(),
                diffused,
            ),
        }
        generation = terraingen.CraterGenerationOptions(min_diameter=4.0, max_diameter=150.0)
        self.heights, self.ratios = {}, {}
        for name, degradation in models.items():
            crater_models = terraingen.CraterModels(
                density, ages, degradation, terraingen.FassettThomsonProfile()
            )
            craters = terraingen.CraterLayer(
                crater_models, terraingen.CraterLayerOptions(seed=31, generation=generation)
            )
            terrain = terraingen.Terrain(terraingen.FlatSource(257, 257, PIXEL), [craters])
            self.heights[name] = terraingen.evaluate(terrain)
            self.ratios[name] = terraingen.placed_craters(terrain).table["depth_ratio"]
            print(f"{name:18s} mean depth/diameter {self.ratios[name].mean():.3f}")
        for name in ("freshness", "none", "fixed d/D 0.1", "diffusion FT2014", "diffusion F2022"):
            self.show(name, self.heights[name])

    def save(self, out):
        shades = [hillshade(heights.values, PIXEL) for heights in self.heights.values()]
        print("wrote", write_png(out / "example_craters_degradation.png", montage(shades, columns=5)))

    def test_final(self):
        ratios = self.ratios
        assert len({len(ratio) for ratio in ratios.values()}) == 1, "every model has the same craters"
        assert len(ratios["none"]) > 10, "more than 10 craters"
        assert np.allclose(ratios["fixed d/D 0.1"], 0.1), "the fixed model keeps d/D at 0.1"
        assert all(
            ratios[name].mean() < ratios["none"].mean()
            for name in ("freshness", "diffusion FT2014", "diffusion F2022")
        ), "every degradation model makes craters shallower than fresh"
        assert all(np.all(np.isfinite(heights.values)) for heights in self.heights.values()), (
            "every height is finite"
        )


if __name__ == "__main__":
    _common.main(Example)
