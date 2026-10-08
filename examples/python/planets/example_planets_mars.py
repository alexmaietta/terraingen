"""Martian terrain: Martian craters and wind-blown bedforms.

`mars_surface` gives Martian crater densities and ages (Ivanov's chronology and production, valid
from 15 m), and Martian craters have their own fresh shape (`mars_fresh_crater_morphometry`).
`BedformLayer` adds ripples and dunes, here Mars's large ripples. Writes heights and a hillshade.

    python example_planets_mars.py [OUTPUT_DIR]
"""

import pathlib
import sys

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, output_dir, write_png

PIXEL = 1.0


def main():
    out = output_dir()
    surface = terraingen.mars_surface(3.0)
    fresh = terraingen.mars_fresh_crater_morphometry
    print(f"fresh Martian 100 m crater: depth/diameter {fresh(100.0).depth_ratio:.3f}")
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages, fresh=fresh), terraingen.FassettThomsonProfile()
    )
    generation = terraingen.CraterGenerationOptions(min_diameter=15.0, max_diameter=150.0)
    craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=2, generation=generation))
    ripples = terraingen.mars_large_ripples()
    ripples.cover, ripples.field_size, ripples.wind_azimuth = 0.6, 80.0, 60.0
    print(f"large ripples: wavelength {ripples.wavelength} m, height {ripples.height} m")
    noise = terraingen.NoiseRelief([terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=150.0, amplitude=2.0, octaves=6))])
    terrain = terraingen.Terrain(terraingen.FlatSource(513, 513, PIXEL), [noise, craters, terraingen.BedformLayer(ripples)])
    evaluated = terraingen.evaluate(terrain)
    print(f"{len(evaluated.craters['x'])} craters")
    terraingen.write_geotiff(out / "example_planets_mars.tif", evaluated.heights)
    print("wrote", write_png(out / "example_planets_mars.png", hillshade(evaluated.heights.values, PIXEL)))


if __name__ == "__main__":
    main()
