"""Where craters go: generated positions, spacing and a cap on their number.

Crater positions are drawn per cell with counter-based random numbers, so any region gives the same
craters as any larger one. `CraterLayer.craters(region)` lists them without evaluating heights.
`CraterSpacing` keeps craters of similar size apart (no two within `separation` larger diameters
when their sizes differ by at most `diameter_ratio`; off by default), and `max_craters` keeps only
that many.
Prints counts and nearest-neighbour distances and writes a montage of hillshades.

    python example_craters_positioning.py [OUTPUT_DIR]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from _common import hillshade, montage, output_dir, write_png

REGION = (-256.0, -256.0, 256.0, 256.0)


def nearest_neighbour(x, y):
    d = np.hypot(x[:, None] - x[None, :], y[:, None] - y[None, :])
    np.fill_diagonal(d, np.inf)
    return d.min(axis=1)


def main():
    out = output_dir()
    surface = terraingen.lunar_surface(3.5)
    models = terraingen.CraterModels(
        surface.density, surface.ages, terraingen.FreshnessDegradation(surface.ages), terraingen.FassettThomsonProfile()
    )
    settings = {
        "no spacing": terraingen.CraterGenerationOptions(spacing=terraingen.CraterSpacing(separation=0.0)),
        "some spacing": terraingen.CraterGenerationOptions(spacing=terraingen.CraterSpacing(diameter_ratio=1.5, separation=0.5)),
        "wide spacing": terraingen.CraterGenerationOptions(spacing=terraingen.CraterSpacing(diameter_ratio=3.0, separation=1.5)),
        "at most 100": terraingen.CraterGenerationOptions(max_craters=100),
    }
    shades = []
    for name, generation in settings.items():
        generation.min_diameter, generation.max_diameter = 6.0, 120.0
        layer = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=8, generation=generation))
        table = layer.craters(REGION)
        gaps = nearest_neighbour(table["x"], table["y"]) / table["diameter"]
        print(f"{name:16s} {len(table['x']):5d} craters, median nearest neighbour {np.median(gaps):.2f} diameters")
        heights = terraingen.evaluate(terraingen.Terrain(terraingen.FlatSource(513, 513, 1.0), [layer])).heights.values
        shades.append(hillshade(heights, 1.0))
    print("wrote", write_png(out / "example_craters_positioning.png", montage(shades, columns=4)))


if __name__ == "__main__":
    main()
