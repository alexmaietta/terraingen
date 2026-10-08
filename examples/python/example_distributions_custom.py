"""A crater size-frequency distribution of your own, written in Python.

Subclass `SizeFrequencyDistribution` and implement `cumulative_at(diameter)`; TerrainGen then draws
craters from it like any built-in distribution. This one is a power law that rolls over below a
knee diameter, as crater counts do where small craters are erased faster than they form. The
example checks that the generated counts match the expected ones and writes the terrain.

    python example_distributions_custom.py [OUTPUT_DIR]
"""

import numpy as np

import terraingen
from _common import hillshade, output_dir, write_png


class RolledOverPowerLaw(terraingen.SizeFrequencyDistribution):
    """N(>= D) = coefficient * D^-slope above `knee` metres, flattening to D^-shallow below it."""

    def __init__(self, coefficient=0.05, slope=2.5, shallow=1.2, knee=20.0):
        super().__init__(terraingen.SfdKind.DENSITY, 1.0, 10000.0)
        self.coefficient, self.slope, self.shallow, self.knee = coefficient, slope, shallow, knee

    def cumulative_at(self, diameter):
        if diameter >= self.knee:
            return self.coefficient * diameter**-self.slope
        at_knee = self.coefficient * self.knee**-self.slope
        return at_knee * (diameter / self.knee) ** -self.shallow


def main():
    out = output_dir()
    density = RolledOverPowerLaw()
    ages = terraingen.UniformAges(3.0)
    models = terraingen.CraterModels(
        density, ages, terraingen.FreshnessDegradation(ages), terraingen.FassettThomsonProfile()
    )
    generation = terraingen.CraterGenerationOptions(min_diameter=6.0, max_diameter=300.0)
    craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=5, generation=generation))
    fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=800.0, amplitude=40.0))
    terrain = terraingen.Terrain(terraingen.FlatSource(513, 513, 2.0), [terraingen.NoiseRelief([fbm]), craters])
    evaluated = terraingen.evaluate(terrain)

    area = (513 * 2.0) ** 2
    diameters = evaluated.craters["diameter"]
    print(f"{'D (m)':>6s} {'expected':>9s} {'generated':>9s}")
    for d in (6, 12, 20, 40, 80):
        expected = density.expected_count(area, d, 300.0)
        print(f"{d:6d} {expected:9.1f} {np.count_nonzero(diameters >= d):9d}")
    terraingen.write_geotiff(out / "example_distributions_custom.tif", evaluated.heights)
    print("wrote", write_png(out / "example_distributions_custom.png", hillshade(evaluated.heights.values, 2.0)))


if __name__ == "__main__":
    main()
