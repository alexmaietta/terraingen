"""How crater age models change a cratered terrain.

Every crater gets an age within a lifetime that depends on its size. `FreshAges` makes every crater
new; `UniformAges` spreads ages over one lifetime for every size; `SteadyStateAges` gives each size
the time it survives before equilibrium cratering replaces it, so small craters are young and large
ones as old as the surface. A custom model, written in Python by subclassing `CraterAgeModel`,
makes lifetimes grow with diameter. Diffusion degradation shows the difference, since it smooths
each crater by its absolute age. Prints age percentiles and writes a montage of hillshades.

    python example_ages_models.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, montage, write_png

PIXEL = 2.0


class SizeLimitedLifetimes(terraingen.CraterAgeModel):
    """A crater D metres across lasts D / 50 Gyr, up to the surface's 3.5 Gyr."""

    def lifetime_at(self, diameter):
        return min(diameter / 50.0, 3.5)


class Example(_common.Example):
    def generate(self):
        surface = terraingen.lunar_surface(3.5)
        steady = terraingen.SteadyStateAges(terraingen.extended_neukum_production_function(), terraingen.trask_equilibrium(), 3.5)
        age_models = {
            "fresh": terraingen.FreshAges(),
            "uniform 3.5 Gyr": terraingen.UniformAges(3.5),
            "steady state": steady,
            "custom": SizeLimitedLifetimes(),
        }
        diffusion = terraingen.DiffusionDegradation(
            terraingen.fassett_2022_hartmann_diffusivity(), terraingen.DiffusedProfile(terraingen.FassettThomsonProfile())
        )
        fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=800.0, amplitude=40.0))
        generation = terraingen.CraterGenerationOptions(min_diameter=8.0, max_diameter=300.0)
        self.evaluated = {}
        print(f"{'ages':16s} {'10%':>7s} {'50%':>7s} {'90%':>7s}  (Gyr)")
        for name, ages in age_models.items():
            models = terraingen.CraterModels(surface.density, ages, diffusion, terraingen.FassettThomsonProfile())
            craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=1, generation=generation))
            terrain = terraingen.Terrain(terraingen.FlatSource(257, 257, PIXEL), [terraingen.NoiseRelief([fbm]), craters])
            evaluated = terraingen.evaluate(terrain)
            p10, p50, p90 = np.percentile(evaluated.craters["age_gyr"], [10, 50, 90])
            print(f"{name:16s} {p10:7.3f} {p50:7.3f} {p90:7.3f}")
            self.evaluated[name] = evaluated
            self.show(name, evaluated.heights)

    def save(self, out):
        shades = [hillshade(evaluated.heights.values, PIXEL) for evaluated in self.evaluated.values()]
        print("wrote", write_png(out / "example_ages_models.png", montage(shades, columns=4)))

    def test_final(self):
        assert len(self.evaluated) == 4, "four age models"
        assert all(e.heights.shape == (257, 257) for e in self.evaluated.values()), "the heights are 257 x 257"
        assert len(self.evaluated["fresh"].craters["diameter"]) > 50, "more than 50 craters"
        assert np.all(self.evaluated["fresh"].craters["age_gyr"] == 0.0), "fresh craters have age zero"
        uniform = self.evaluated["uniform 3.5 Gyr"].craters["age_gyr"]
        assert uniform.min() >= 0.0 and uniform.max() <= 3.5, "uniform ages lie within 3.5 Gyr"
        steady = self.evaluated["steady state"].craters["age_gyr"]
        assert np.median(steady) < 0.5 * np.median(uniform), "steady-state craters are mostly younger than uniformly aged ones"
        custom = self.evaluated["custom"].craters
        assert np.all(custom["age_gyr"] <= custom["diameter"] / 50.0 + 1e-9), "custom ages lie within each crater's D / 50 Gyr lifetime"


if __name__ == "__main__":
    _common.main(Example)
