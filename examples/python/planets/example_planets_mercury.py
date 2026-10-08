"""A site on Mercury, on a sphere of Mercury's mean radius.

Any body works as a sphere: `body_sphere(Body.MERCURY)` uses Mercury's mean radius from NASA's
planetary fact sheet. TerrainGen has no Mercury crater calibration, so this example uses Trask's
lunar equilibrium as a stand-in for a saturated, heavily cratered surface; give your own
distribution (see example_distributions_custom.py) for a calibrated one. Writes heights and a
hillshade.

    python example_planets_mercury.py [OUTPUT_DIR]
"""

import pathlib
import sys

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, output_dir, write_png

PIXEL = 200.0


def main():
    out = output_dir()
    mercury = terraingen.body_sphere(terraingen.Body.MERCURY)
    print(f"Mercury radius {mercury.radius / 1000:.1f} km, {mercury.unit_length:.0f} m per degree")
    noise = terraingen.NoiseRelief(
        [
            terraingen.NoiseLayer(terraingen.FractalParams(seed=4, wavelength=300000.0, amplitude=2500.0, octaves=6)),
            terraingen.NoiseLayer(terraingen.FractalParams(seed=5, wavelength=15000.0, amplitude=250.0, octaves=6)),
        ],
        surface=mercury,
    )
    ages = terraingen.UniformAges(4.0)
    models = terraingen.CraterModels(
        terraingen.trask_equilibrium(), ages, terraingen.FreshnessDegradation(ages), terraingen.FassettThomsonProfile()
    )
    generation = terraingen.CraterGenerationOptions(min_diameter=1000.0, max_diameter=30000.0)
    craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=4, generation=generation, surface=mercury))
    site = terraingen.FlatSource(257, 257, PIXEL, centre=(-30.0, 10.0), surface=mercury)
    evaluated = terraingen.evaluate(terraingen.Terrain(site, [noise, craters], surface=mercury))
    print(f"{len(evaluated.craters['x'])} craters of 1 to 30 km over a {257 * PIXEL / 1000:.0f} km site")
    terraingen.write_geotiff(out / "example_planets_mercury.tif", evaluated.heights)
    print("wrote", write_png(out / "example_planets_mercury.png", hillshade(evaluated.heights.values, PIXEL)))


if __name__ == "__main__":
    main()
