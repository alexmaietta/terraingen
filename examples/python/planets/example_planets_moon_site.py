"""A site on the Moon, on a sphere: longitude and latitude coordinates and metres everywhere else.

On a sphere surface, grid coordinates are degrees of longitude and latitude, but pixel sizes,
wavelengths and crater diameters stay in metres. Noise is 3D at each point's position, so it is
seamless across the antimeridian and the poles, and craters are placed by longitude and latitude
and shaped in their own local frame. This example builds a 51 km site at Mare Tranquillitatis,
queries points by longitude and latitude, converts between degrees and local metres, and writes
the heights (with a geographic CRS) and a hillshade.

    python example_planets_moon_site.py [OUTPUT_DIR]
"""

import pathlib
import sys

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, output_dir, write_png

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
    surface = terraingen.lunar_surface(3.8)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    generation = terraingen.CraterGenerationOptions(min_diameter=1000.0, max_diameter=30000.0)
    craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=3, generation=generation, surface=moon))
    site = terraingen.FlatSource(257, 257, PIXEL, centre=SITE, surface=moon)
    terrain = terraingen.Terrain(site, [noise, craters], surface=moon)

    print(f"Moon radius {moon.radius / 1000:.1f} km; {moon.unit_length:.1f} m per degree of latitude")
    print(f"grid cells are {site.transform.pixel_width:.5f} degrees ({PIXEL:.0f} m)")
    east = moon.locate(SITE, (10000.0, 0.0))
    print(f"10 km east of the site is ({east[0]:.4f}, {east[1]:.4f}); distance {moon.distance(SITE, east):.1f} m")
    evaluated = terraingen.evaluate(terrain)
    print(f"{len(evaluated.craters['x'])} craters; height at the site {terraingen.evaluate_at(terrain, *SITE, PIXEL):.1f} m")
    terraingen.write_geotiff(out / "example_planets_moon_site.tif", evaluated.heights)
    print("wrote", write_png(out / "example_planets_moon_site.png", hillshade(evaluated.heights.values, PIXEL)))


if __name__ == "__main__":
    main()
