"""Rim smoothing: soften the sharp crests a crater profile leaves.

`RimSmoothingFilter` blurs each crater's rim with a Gaussian of `sigma_radii` crater radii, over a
band from `inner_radii` to `outer_radii` that fades along `shape`, at `strength` from 0 to 1. It is a
filter, so it runs after every layer and reads the craters they applied. Prints the highest rim in
each case and writes a montage of hillshades.

    python example_craters_rim_smoothing.py [OUTPUT_DIR]
"""

import pathlib
import sys

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, montage, output_dir, write_png

PIXEL = 1.0


def main():
    out = output_dir()
    surface = terraingen.lunar_surface(3.5)
    models = terraingen.CraterModels(surface.density, surface.ages, terraingen.NoDegradation(), terraingen.FassettThomsonProfile())
    generation = terraingen.CraterGenerationOptions(min_diameter=10.0, max_diameter=120.0)
    craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=3, generation=generation))
    source = terraingen.FlatSource(257, 257, PIXEL)
    variants = {
        "none": None,
        "default": terraingen.RimSmoothing(),
        "wide": terraingen.RimSmoothing(sigma_radii=0.15, inner_radii=0.6, outer_radii=1.4),
        "half strength": terraingen.RimSmoothing(sigma_radii=0.15, strength=0.5),
    }
    shades = []
    for name, options in variants.items():
        filters = [terraingen.RimSmoothingFilter(options)] if options else []
        heights = terraingen.evaluate(terraingen.Terrain(source, [craters], filters)).heights.values
        print(f"{name:14s} highest rim {heights.max():.2f} m")
        shades.append(hillshade(heights, PIXEL))
    print("wrote", write_png(out / "example_craters_rim_smoothing.png", montage(shades, columns=4)))


if __name__ == "__main__":
    main()
