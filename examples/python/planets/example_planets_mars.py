"""Martian terrain: Martian craters and wind-blown bedforms.

A `ProductionSurface` of Ivanov's production and chronology gives Martian crater densities (valid
from 15 m), with ages uniform over the surface's age, and Martian craters have their own fresh
shape (`mars_fresh_crater_morphometry`).
`BedformLayer` adds ripples and dunes, here Mars's large ripples. Writes heights and a hillshade.

    python example_planets_mars.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, write_png

PIXEL = 1.0


class Example(_common.Example):
    def generate(self):
        density = terraingen.ProductionSurface(
            terraingen.ivanov_mars_production_function(), 3.0, terraingen.ivanov_mars_chronology
        )
        ages = terraingen.UniformAges(3.0)
        fresh = terraingen.mars_fresh_crater_morphometry
        self.depth_ratio = fresh(100.0).depth_ratio
        print(f"fresh Martian 100 m crater: depth/diameter {self.depth_ratio:.3f}")
        models = terraingen.CraterModels(
            density,
            ages,
            terraingen.FreshnessDegradation(ages, fresh=fresh),
            terraingen.FassettThomsonProfile(),
        )
        generation = terraingen.CraterGenerationOptions(d_min=15.0, d_max=150.0)
        craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=2, generation=generation))
        self.ripples = terraingen.mars_large_ripples()
        self.ripples.cover, self.ripples.field_size, self.ripples.wind_azimuth_deg = 0.6, 80.0, 60.0
        print(f"large ripples: wavelength {self.ripples.wavelength} m, height {self.ripples.height} m")
        noise = terraingen.NoiseRelief(
            [
                terraingen.NoiseLayer(
                    terraingen.FractalParams(seed=2, wavelength=150.0, amplitude=2.0, octaves=6)
                )
            ]
        )
        terrain = terraingen.TerrainGenerator(
            terraingen.FlatSource(513, 513, PIXEL), [noise, craters, terraingen.BedformLayer(self.ripples)]
        )
        self.heights = terraingen.evaluate(terrain)
        self.craters = terraingen.placed_craters(terrain).table
        print(f"{len(self.craters['x'])} craters")
        self.show("heights", self.heights)

    def save(self, out):
        terraingen.write_geotiff(out / "example_planets_mars.tif", self.heights)
        print(
            "wrote",
            write_png(out / "example_planets_mars.png", hillshade(self.heights.values, PIXEL)),
        )

    def test_final(self):
        heights = self.heights.values
        diameters = self.craters["diameter"]
        assert heights.shape == (513, 513) and np.isfinite(heights).all(), "finite 513 x 513 heights"
        assert 0.1 < self.depth_ratio < 0.3, (
            "a fresh Martian crater is 0.1 to 0.3 times as deep as it is wide"
        )
        assert 0.5 < self.ripples.wavelength < 10.0 and 0.0 < self.ripples.height < 1.0, (
            "large ripples are meters apart and decimeters high"
        )
        assert len(diameters) > 100, "more than 100 craters"
        assert diameters.min() >= 15.0 and diameters.max() <= 150.0, "every crater is 15 to 150 m across"


if __name__ == "__main__":
    _common.main(Example)
