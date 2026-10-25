# SPDX-License-Identifier: BSD-3-Clause

"""Boulders on the steep walls of a fresh 100 m crater on fractal relief, which throws blocks of its
own, drawn to scale over the crater's hillshade and colored by the slope of the ground beneath them.
Left: boulders rest wherever they land. Middle: none stay on ground steeper than 20 degrees. Right:
those instead roll up to 40 m downhill and stop where the wall eases below 20 degrees, gathering in a
ring partway down it."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

DIAMETER = 100.0
HALF_SIZE_M = 90.0
PANELS = [
    ("Resting anywhere", None, None),
    ("None steeper than 20 degrees", 20.0, None),
    ("Rolling up to 40 m downhill", 20.0, 40.0),
]


def figure(quick=False):
    pixel = 1.0 if quick else 0.25
    size = int(2.0 * HALF_SIZE_M / pixel) + 1
    relief = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=400.0, amplitude=12.0))]
    )
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)
    craters = terraingen.CraterLayer.manual([terraingen.Crater(0.0, 0.0, DIAMETER, age_gyr=0.0)])
    ground = terraingen.evaluate(
        terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief, craters])
    ).values
    slopes = gallery.slopes_degrees(ground, pixel)

    fig, axes = plt.subplots(1, len(PANELS), figsize=(15, 4.9), layout="constrained")
    for ax, (title, max_slope, roll) in zip(axes, PANELS, strict=True):
        options = terraingen.BoulderLayerOptions(seed=4)
        sizes = options.sizes
        sizes.min_diameter = 0.4
        options.sizes = sizes
        ejecta = options.ejecta
        ejecta.min_crater_diameter = 10.0
        options.ejecta = ejecta
        pose = options.pose
        pose.max_slope_degrees = max_slope
        pose.roll_distance = roll
        options.pose = pose
        boulders = terraingen.BoulderLayer(options, craters)
        terrain = terraingen.TerrainGenerator(
            terraingen.FlatSource(size, size, pixel), [relief, craters, boulders]
        )
        table = terraingen.placed_boulders(terrain).table

        rows = np.clip(((HALF_SIZE_M - np.asarray(table["y"])) / pixel).astype(int), 0, size - 1)
        columns = np.clip(((np.asarray(table["x"]) + HALF_SIZE_M) / pixel).astype(int), 0, size - 1)
        ax.imshow(
            gallery.LIGHT.hillshade(ground, dx=pixel, dy=pixel),
            cmap="gray",
            vmin=-0.6,
            vmax=1.4,
            extent=gallery.extent(ground, pixel),
        )
        circles = gallery.show_boulders(
            ax, table, region, slopes[rows, columns], cmap="plasma", vmin=0.0, vmax=35.0, colorbar=False
        )
        ax.set_title(f"{title}: {len(table['diameter'])} boulders")
    fig.colorbar(circles, ax=axes, label="Ground slope (degrees)", shrink=0.85)
    return fig
