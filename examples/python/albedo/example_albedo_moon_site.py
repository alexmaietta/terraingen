"""Albedo on a sphere: bright crater material at a lunar site in longitude and latitude.

Albedo sources and layers take a surface like terrain layers do. On the Moon's sphere the synthetic
albedo's noise is 3D at each point's position, and the crater albedo layer reads the craters of a
crater layer placed by longitude and latitude, giving a 2 Myr old 4 km crater its bright ejecta and
rays. Fresh material brightens in proportion to how far the background reflectance lies above the
maturity model's origin (`CraterAlbedoOptions.origin_reflectance`, 0.08), so the background here
is 0.11, mature mare soil. Writes the albedo and a hillshade.

    python example_albedo_moon_site.py [OUTPUT_DIR]
"""

import pathlib
import sys

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, montage, normalize, output_dir, write_png

SITE = (23.47, 0.67)
PIXEL = 200.0


def main():
    out = output_dir()
    moon = terraingen.body_sphere(terraingen.Body.MOON)
    noise = terraingen.NoiseRelief(
        [
            terraingen.NoiseLayer(terraingen.FractalParams(seed=8, wavelength=400000.0, amplitude=3000.0, octaves=6)),
            terraingen.NoiseLayer(terraingen.FractalParams(seed=9, wavelength=20000.0, amplitude=300.0, octaves=6)),
        ],
        surface=moon,
    )
    surface = terraingen.lunar_surface(3.6)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    young = terraingen.Crater(23.5, 0.7, 4000.0, age_gyr=0.002)
    generation = terraingen.CraterGenerationOptions(min_diameter=1000.0, max_diameter=30000.0, manual=[young])
    craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=3, generation=generation, surface=moon))
    site = terraingen.FlatSource(257, 257, PIXEL, centre=SITE, surface=moon)
    terrain = terraingen.Terrain(site, [noise, craters], surface=moon)

    mottling = terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=30000.0, amplitude=0.05, octaves=5))
    albedo = terraingen.Albedo(
        terraingen.SyntheticAlbedo(value=0.11, value_noise=[mottling], surface=moon),
        [terraingen.CraterAlbedo(craters, terraingen.CraterAlbedoOptions(seed=3))],
        surface=moon,
    )
    evaluated = terraingen.evaluate(terrain)
    bands = terraingen.evaluate_albedo(albedo, site.grid())
    band = bands[0].values
    print(f"{len(evaluated.craters['x'])} craters; albedo {band.min():.3f} to {band.max():.3f}")
    terraingen.write_geotiff(out / "example_albedo_moon_site.tif", bands)

    shades = [normalize(band), hillshade(evaluated.heights.values, PIXEL)]
    print("wrote", write_png(out / "example_albedo_moon_site.png", montage(shades, columns=2)))


if __name__ == "__main__":
    main()
