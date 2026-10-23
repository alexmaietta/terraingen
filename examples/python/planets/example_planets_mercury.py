# SPDX-License-Identifier: BSD-3-Clause

"""A site on Mercury, on a sphere of Mercury's mean radius.

Any body works as a sphere: `Surface.sphere(radius)` with the body's radius, here Mercury's mean radius
from NASA's planetary fact sheet. TerrainGen has no Mercury crater calibration, so this example uses Trask's
lunar equilibrium as a stand-in for a saturated, heavily cratered surface; give your own
distribution (see example_distributions_custom.py) for a calibrated one. Writes heights and a
hillshade.

    python example_planets_mercury.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, write_png

PIXEL = 200.0
# Volumetric mean radius of Mercury in meters, from the NASA NSSDCA Mercury fact sheet.
MERCURY_RADIUS = 2439700.0


class Example(_common.Example):
    def generate(self):
        self.mercury = terraingen.Surface.sphere(MERCURY_RADIUS)
        print(
            f"Mercury radius {self.mercury.radius / 1000:.1f} km, {self.mercury.unit_length:.0f} m per degree"
        )
        noise = terraingen.NoiseRelief(
            [
                terraingen.NoiseLayer(
                    terraingen.FractalParams(seed=4, wavelength=300000.0, amplitude=2500.0, octaves=6)
                ),
                terraingen.NoiseLayer(
                    terraingen.FractalParams(seed=5, wavelength=15000.0, amplitude=250.0, octaves=6)
                ),
            ],
            surface=self.mercury,
        )
        ages = terraingen.UniformAges(4.0)
        models = terraingen.CraterModels(
            terraingen.trask_equilibrium(),
            ages,
            terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
            terraingen.FassettThomsonProfile(),
        )
        generation = terraingen.CraterGenerationOptions(d_min=1000.0, d_max=30000.0)
        craters = terraingen.CraterLayer(
            models, terraingen.CraterLayerOptions(seed=4, generation=generation, surface=self.mercury)
        )
        site = terraingen.FlatSource(257, 257, PIXEL, center=(-30.0, 10.0), surface=self.mercury)
        terrain = terraingen.TerrainGenerator(site, [noise, craters], surface=self.mercury)
        self.heights = terraingen.evaluate(terrain)
        self.craters = terraingen.placed_craters(terrain).table
        print(f"{len(self.craters['x'])} craters of 1 to 30 km over a {257 * PIXEL / 1000:.0f} km site")
        self.show("heights", self.heights.values, PIXEL)

    def save(self, out):
        terraingen.write_geotiff(out / "example_planets_mercury.tif", self.heights)
        print(
            "wrote",
            write_png(out / "example_planets_mercury.png", hillshade(self.heights.values, PIXEL)),
        )

    def test_final(self):
        heights = self.heights.values
        diameters = self.craters["diameter"]
        assert abs(self.mercury.radius - 2439700.0) < 1000.0, "Mercury's radius is 2440 km"
        assert heights.shape == (257, 257) and np.isfinite(heights).all(), "finite 257 x 257 heights"
        assert len(diameters) > 50, "more than 50 craters"
        assert diameters.min() >= 1000.0 and diameters.max() <= 30000.0, "every crater is 1 to 30 km across"
        assert np.ptp(heights) > 100.0 and np.abs(heights).max() < 20000.0, (
            "hundreds of meters to kilometers of relief"
        )


if __name__ == "__main__":
    _common.main(Example)
