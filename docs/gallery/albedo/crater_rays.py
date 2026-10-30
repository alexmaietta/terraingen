# SPDX-License-Identifier: BSD-3-Clause

"""A 400 m crater's bright interior, ejecta and rays on a 3 km square, in optical maturity. The top row
shows the crater fresh, then 1, 3 and 6 Myr old, its rays fading over their retention time and its
ejecta after them. The bottom row, all fresh, shows smooth rays, fully patchy rays, and ejecta broken
into radial streaks and into lobes."""

import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Impacts and landers"

SIZE_M = 3000.0
DIAMETER = 400.0


def figure(quick=False):
    pixel = 20.0 if quick else 5.0
    size = int(SIZE_M / pixel) + 1
    terrain = terraingen.Terrain(source=terraingen.FlatSource(size, size, pixel))
    grid = terraingen.RasterGrid.from_raster(terrain.evaluate())
    base = terraingen.SyntheticAlbedo(value=0.11)

    def options(patchiness=None, streaks=0.0, lobes=0.0):
        settings = terraingen.CraterAlbedoOptions(seed=2)
        rays = settings.rays
        rays.oblique.enabled = False
        oblique = rays.oblique
        oblique.enabled = False
        rays.oblique = oblique
        if patchiness is not None:
            rays.patchiness = patchiness
        settings.rays = rays
        texture = settings.texture
        texture.streaks = streaks
        texture.lobes = lobes
        settings.texture = texture
        return settings

    panels = [
        (f"{age * 1000:g} Myr old" if age else "Fresh", age, options()) for age in (0.0, 0.001, 0.003, 0.006)
    ]
    panels += [
        ("Smooth rays", 0.0, options(patchiness=0.0)),
        ("Patchy rays", 0.0, options(patchiness=1.0)),
        ("Streaked ejecta", 0.0, options(streaks=0.6)),
        ("Lobed ejecta", 0.0, options(lobes=0.6)),
    ]

    fig, axes = plt.subplots(2, 4, figsize=(15, 8.2), layout="constrained")
    for ax, (title, age, settings) in zip(axes.flat, panels, strict=True):
        crater = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=age)])
        albedo = terraingen.Albedo(base, [terraingen.CraterAlbedo(crater, settings)])
        values = terraingen.evaluate_albedo(albedo, grid)[0].values
        image = ax.imshow(values, cmap="gray", vmin=0.1, vmax=0.15)
        ax.set_axis_off()
        ax.set_title(title)
    fig.colorbar(image, ax=axes, label="Albedo", shrink=0.85)
    return fig
