"""A crater size-frequency distribution of your own, written in Python.

Subclass `SizeFrequencyDistribution` and implement `cumulative_at(diameter)`; TerrainGen then draws
craters from it like any built-in distribution. This one is a power law that rolls over below a
knee diameter, as crater counts do where small craters are erased faster than they form. The
example checks that the generated counts match the expected ones and writes the terrain.

    python example_distributions_custom.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, write_png


class RolledOverPowerLaw(terraingen.SizeFrequencyDistribution):
    """N(>= D) = coefficient * D^-slope above `knee` meters, flattening to D^-shallow below it."""

    def __init__(self, coefficient=0.05, slope=2.5, shallow=1.2, knee=20.0):
        super().__init__(terraingen.SfdKind.DENSITY, 1.0, 10000.0)
        self.coefficient, self.slope, self.shallow, self.knee = coefficient, slope, shallow, knee

    def cumulative_at(self, diameter):
        if diameter >= self.knee:
            return self.coefficient * diameter**-self.slope
        at_knee = self.coefficient * self.knee**-self.slope
        return at_knee * (diameter / self.knee) ** -self.shallow


class Example(_common.Example):
    def generate(self):
        self.density = RolledOverPowerLaw()
        ages = terraingen.UniformAges(3.0)
        models = terraingen.CraterModels(
            self.density,
            ages,
            terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
            terraingen.FassettThomsonProfile(),
        )
        generation = terraingen.CraterGenerationOptions(d_min=6.0, d_max=300.0)
        craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=5, generation=generation))
        fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=800.0, amplitude=40.0))
        terrain = terraingen.TerrainGenerator(
            terraingen.FlatSource(513, 513, 2.0), [terraingen.NoiseRelief([fbm]), craters]
        )
        self.heights = terraingen.evaluate(terrain)
        self.craters = terraingen.placed_craters(terrain).table

        self.area = (513 * 2.0) ** 2
        diameters = self.craters["diameter"]
        print(f"{'D (m)':>6s} {'expected':>9s} {'generated':>9s}")
        for d in (6, 12, 20, 40, 80):
            expected = self.density.expected_count(self.area, d, 300.0)
            print(f"{d:6d} {expected:9.1f} {np.count_nonzero(diameters >= d):9d}")
        self.show("heights", self.heights)

    def save(self, out):
        terraingen.write_geotiff(out / "example_distributions_custom.tif", self.heights)
        print(
            "wrote",
            write_png(out / "example_distributions_custom.png", hillshade(self.heights.values, 2.0)),
        )

    def test_final(self):
        diameters = self.craters["diameter"]
        assert self.heights.shape == (513, 513)
        assert diameters.min() >= 6.0 and diameters.max() <= 300.0, "every crater is 6 to 300 m across"
        expected = self.density.expected_count(self.area, 6.0, 300.0)
        assert 0.7 * expected < len(diameters) < 1.3 * expected, (
            "the craters drawn number within 30% of those expected"
        )
        counts = [np.count_nonzero(diameters >= d) for d in (6, 12, 40)]
        assert counts[0] > counts[1] > counts[2], "fewer craters at larger sizes"
        assert np.all(np.isfinite(self.heights.values)), "the heights are finite"


if __name__ == "__main__":
    _common.main(Example)
