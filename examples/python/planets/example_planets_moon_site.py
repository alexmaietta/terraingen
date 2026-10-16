"""A site on the Moon, on a sphere: longitude and latitude coordinates and metres everywhere else.

On a sphere surface, grid coordinates are degrees of longitude and latitude, but pixel sizes,
wavelengths and crater diameters stay in metres. Noise is 3D at each point's position, so it is
seamless across the antimeridian and the poles, and craters are placed by longitude and latitude
and shaped in their own local frame. This example builds a 51 km site at Mare Tranquillitatis,
queries points by longitude and latitude, converts between degrees and local metres, and writes
the heights (with a geographic CRS) and a hillshade.

    python example_planets_moon_site.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, write_png

SITE = (23.47, 0.67)
PIXEL = 200.0


class Example(_common.Example):
    def generate(self):
        self.moon = terraingen.body_sphere(terraingen.Body.MOON)
        noise = terraingen.NoiseRelief(
            [
                terraingen.NoiseLayer(
                    terraingen.FractalParams(seed=8, wavelength=400000.0, amplitude=3000.0, octaves=6)
                ),
                terraingen.NoiseLayer(
                    terraingen.FractalParams(seed=9, wavelength=20000.0, amplitude=300.0, octaves=6)
                ),
            ],
            surface=self.moon,
        )
        surface = terraingen.lunar_surface(3.8)
        models = terraingen.CraterModels(
            surface.density,
            surface.ages,
            terraingen.FreshnessDegradation(surface.ages),
            terraingen.FassettThomsonProfile(),
        )
        generation = terraingen.CraterGenerationOptions(min_diameter=1000.0, max_diameter=30000.0)
        craters = terraingen.CraterLayer(
            models, terraingen.CraterLayerOptions(seed=3, generation=generation, surface=self.moon)
        )
        self.site = terraingen.FlatSource(257, 257, PIXEL, centre=SITE, surface=self.moon)
        terrain = terraingen.Terrain(self.site, [noise, craters], surface=self.moon)

        print(
            f"Moon radius {self.moon.radius / 1000:.1f} km; {self.moon.unit_length:.1f} m per degree of latitude"
        )
        print(f"grid cells are {self.site.transform.pixel_width:.5f} degrees ({PIXEL:.0f} m)")
        self.east = self.moon.locate(SITE, (10000.0, 0.0))
        self.distance = self.moon.distance(SITE, self.east)
        print(
            f"10 km east of the site is ({self.east[0]:.4f}, {self.east[1]:.4f}); distance {self.distance:.1f} m"
        )
        self.evaluated = terraingen.evaluate(terrain)
        self.site_height = terraingen.evaluate_at(terrain, *SITE, PIXEL)
        print(f"{len(self.evaluated.craters['x'])} craters; height at the site {self.site_height:.1f} m")
        self.show("heights", self.evaluated.heights.values, PIXEL)

    def save(self, out):
        terraingen.write_geotiff(out / "example_planets_moon_site.tif", self.evaluated.heights)
        print(
            "wrote",
            write_png(out / "example_planets_moon_site.png", hillshade(self.evaluated.heights.values, PIXEL)),
        )

    def test_final(self):
        heights = self.evaluated.heights.values
        assert heights.shape == (257, 257) and np.isfinite(heights).all(), "finite 257 x 257 heights"
        assert abs(self.site.transform.pixel_width * self.moon.unit_length - PIXEL) < 1.0, (
            "grid cells are 200 m in degrees"
        )
        assert abs(self.distance - 10000.0) < 1.0, "the point located 10 km east is 10 km away"
        assert 23.6 < self.east[0] < 24.0 and abs(self.east[1] - SITE[1]) < 0.01, (
            "10 km east is about a third of a degree east"
        )
        assert len(self.evaluated.craters["x"]) > 10, "more than 10 craters at the site"
        assert np.isfinite(self.site_height) and abs(self.site_height) < 10000.0, (
            "the site's height is within 10 km of the datum"
        )


if __name__ == "__main__":
    _common.main(Example)
