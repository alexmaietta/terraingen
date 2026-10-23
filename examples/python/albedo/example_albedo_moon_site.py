"""Albedo on a sphere: bright crater material at a lunar site in longitude and latitude.

Albedo sources and layers take a surface like terrain layers do. On the Moon's sphere the synthetic
albedo's noise is 3D at each point's position, and the crater albedo layer reads the craters of a
crater layer placed by longitude and latitude, giving a 2 Myr old 4 km crater its bright ejecta and
rays. Fresh material brightens in proportion to how far the background reflectance lies above the
maturity model's origin (`CraterAlbedoOptions.origin_reflectance`, 0.08), so the background here
is 0.11, mature mare soil. Writes the albedo and a hillshade.

    python example_albedo_moon_site.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import hillshade, montage, normalize, write_png

SITE = (23.47, 0.67)
PIXEL = 200.0
# Volumetric mean radius of the Moon in metres, from the NASA NSSDCA Moon fact sheet.
MOON_RADIUS = 1737400.0


class Example(_common.Example):
    def generate(self):
        moon = terraingen.Surface.sphere(MOON_RADIUS)
        noise = terraingen.NoiseRelief(
            [
                terraingen.NoiseLayer(
                    terraingen.FractalParams(seed=8, wavelength=400000.0, amplitude=3000.0, octaves=6)
                ),
                terraingen.NoiseLayer(
                    terraingen.FractalParams(seed=9, wavelength=20000.0, amplitude=300.0, octaves=6)
                ),
            ],
            surface=moon,
        )
        production = terraingen.extended_neukum_production_function()
        equilibrium = terraingen.trask_equilibrium()
        density = terraingen.MinimumSfd(
            [terraingen.ProductionSurface(production, 3.6, terraingen.neukum_lunar_chronology), equilibrium]
        )
        ages = terraingen.SteadyStateAges(production, equilibrium, 3.6, terraingen.neukum_lunar_chronology)
        models = terraingen.CraterModels(
            density,
            ages,
            terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
            terraingen.FassettThomsonProfile(),
        )
        young = terraingen.Crater(23.5, 0.7, 4000.0, age_gyr=0.002)
        generation = terraingen.CraterGenerationOptions(d_min=1000.0, d_max=30000.0, manual=[young])
        craters = terraingen.CraterLayer(
            models, terraingen.CraterLayerOptions(seed=3, generation=generation, surface=moon)
        )
        site = terraingen.FlatSource(257, 257, PIXEL, center=SITE, surface=moon)
        terrain = terraingen.TerrainGenerator(site, [noise, craters], surface=moon)

        mottling = terraingen.NoiseLayer(
            terraingen.FractalParams(seed=2, wavelength=30000.0, amplitude=0.05, octaves=5)
        )
        albedo = terraingen.Albedo(
            terraingen.SyntheticAlbedo(value=0.11, value_noise=[mottling], surface=moon),
            [terraingen.CraterAlbedo(craters, terraingen.CraterAlbedoOptions(seed=3))],
            surface=moon,
        )
        self.heights = terraingen.evaluate(terrain)
        self.craters = terraingen.placed_craters(terrain).table
        self.bands = terraingen.evaluate_albedo(albedo, site.grid())
        band = self.bands[0].values
        print(f"{len(self.craters['x'])} craters; albedo {band.min():.3f} to {band.max():.3f}")
        self.show("heights and albedo", self.heights.values, PIXEL, albedo=band)

    def save(self, out):
        terraingen.write_geotiff(out / "example_albedo_moon_site.tif", self.bands)
        shades = [normalize(self.bands[0].values), hillshade(self.heights.values, PIXEL)]
        print("wrote", write_png(out / "example_albedo_moon_site.png", montage(shades, columns=2)))

    def test_final(self):
        band = self.bands[0].values
        assert self.heights.shape == (257, 257)
        assert band.shape == (257, 257), "the albedo is on the site's grid"
        assert np.all(np.isfinite(band)), "the albedo is finite"
        assert band.min() > 0.05 and band.max() < 0.25, "the albedo stays near 0.11"
        assert band.max() > np.median(band) + 0.005, "the young crater brightens the albedo"
        assert len(self.craters["x"]) >= 5, "several craters at the site"


if __name__ == "__main__":
    _common.main(Example)
