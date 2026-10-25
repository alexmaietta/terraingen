# SPDX-License-Identifier: BSD-3-Clause

"""Rays of a fresh 400 m crater struck from the west, on a 3 km square: radial rays from a vertical
impact, then the patterns of ever lower impact angles: rays reaching farther downrange, a forbidden
zone uprange with no rays, and a butterfly of rays to either side. The angle thresholds are set here
so this one crater falls in each regime."""

import matplotlib.pyplot as plt

import terraingen

WIDE = True

SIZE_M = 3000.0
DIAMETER = 400.0
PANELS = [
    ("Radial", None),
    ("Asymmetric", (90.0, 1.0, 0.5)),
    ("Forbidden zone", (90.0, 89.5, 0.5)),
    ("Butterfly", (90.0, 89.9, 89.8)),
]


def figure(quick=False):
    pixel = 20.0 if quick else 5.0
    size = int(SIZE_M / pixel) + 1
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [])
    grid = terraingen.RasterGrid.from_raster(terraingen.evaluate(terrain))
    base = terraingen.SyntheticAlbedo(value=0.11)
    crater = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.0)])

    fig, axes = plt.subplots(1, len(PANELS), figsize=(16, 4.3), layout="constrained")
    for ax, (title, thresholds) in zip(axes, PANELS, strict=True):
        settings = terraingen.CraterAlbedoOptions(seed=2)
        rays = settings.rays
        oblique = rays.oblique
        oblique.enabled = thresholds is not None
        oblique.heading_degrees = 0.0
        if thresholds is not None:
            oblique.asymmetric_degrees, oblique.forbidden_degrees, oblique.butterfly_degrees = thresholds
        rays.oblique = oblique
        settings.rays = rays
        albedo = terraingen.Albedo(base, [terraingen.CraterAlbedo(crater, settings)])
        values = terraingen.evaluate_albedo(albedo, grid)[0].values
        image = ax.imshow(values, cmap="gray", vmin=0.1, vmax=0.15)
        ax.set_axis_off()
        ax.set_title(title)
    fig.colorbar(image, ax=axes, label="Albedo", shrink=0.85)
    return fig
