"""The same cratered terrain at several surface ages.

`lunar_surface` is the Neukum production for the surface's age, capped at Trask's equilibrium:
young surfaces gain craters quickly, and craters of tens of metres saturate within about a billion
years, after which new craters erase as many as they add. The seed stays fixed, so each panel
differs only by age. Prints counts and writes a montage of hillshades.

    python example_craters_age_sweep.py [OUTPUT_DIR]
"""

import terraingen
from _common import hillshade, montage, output_dir, write_png

PIXEL = 2.0


def main():
    out = output_dir()
    fbm = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=400.0, amplitude=10.0))
    shades = []
    for age in (0.01, 0.05, 0.3, 3.9):
        surface = terraingen.lunar_surface(age)
        models = terraingen.CraterModels(
            surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
        )
        craters = terraingen.CraterLayer(
            models, terraingen.CraterLayerOptions(seed=21, generation=terraingen.CraterGenerationOptions(min_diameter=20.0, max_diameter=200.0))
        )
        terrain = terraingen.Terrain(terraingen.FlatSource(257, 257, PIXEL), [terraingen.NoiseRelief([fbm]), craters])
        evaluated = terraingen.evaluate(terrain)
        print(f"{age:4.2f} Gyr: {len(evaluated.craters['x']):3d} craters of 20 m or more")
        shades.append(hillshade(evaluated.heights.values, PIXEL))
    print("wrote", write_png(out / "example_craters_age_sweep.png", montage(shades, columns=4)))


if __name__ == "__main__":
    main()
