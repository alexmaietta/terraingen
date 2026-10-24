# SPDX-License-Identifier: BSD-3-Clause

"""Nine identical craters from 1 Myr to 1.5 Gyr old, worn down by the default degradation model."""

import gallery
import matplotlib.pyplot as plt
from matplotlib.colors import LightSource

import terraingen

AGES_GYR = [0.001, 0.01, 0.05, 0.1, 0.25, 0.5, 0.75, 1.0, 1.5]
DIAMETER = 150.0
SPACING = 240.0


def age_text(age_gyr):
    return f"{age_gyr * 1000:g} Myr" if age_gyr < 1.0 else f"{age_gyr:g} Gyr"


def figure(quick=False):
    pixel = 4.0 if quick else 0.75
    size = int(3 * SPACING / pixel) + 1
    centers = [((column - 1) * SPACING, (1 - row) * SPACING) for row in range(3) for column in range(3)]
    craters = [
        terraingen.Crater(x, y, DIAMETER, age_gyr=age) for (x, y), age in zip(centers, AGES_GYR, strict=True)
    ]
    relief = terraingen.NoiseRelief(
        [terraingen.NoiseLayer(terraingen.FractalParams(seed=2, wavelength=400.0, amplitude=12.0))]
    )
    terrain = terraingen.TerrainGenerator(
        terraingen.FlatSource(size, size, pixel), [relief, terraingen.CraterLayer.manual(craters)]
    )
    heights = terraingen.evaluate(terrain).values

    light = LightSource(azdeg=315, altdeg=25)
    fig, ax = plt.subplots(figsize=(7, 7), layout="constrained")
    ax.imshow(
        light.hillshade(heights, dx=pixel, dy=pixel),
        cmap="gray",
        vmin=0,
        vmax=1,
        extent=gallery.extent(heights, pixel),
    )
    for (x, y), age in zip(centers, AGES_GYR, strict=True):
        ax.text(
            x,
            y - 0.5 * DIAMETER - 20.0,
            age_text(age),
            ha="center",
            va="top",
            color="white",
            fontsize=9,
            bbox={"facecolor": "black", "alpha": 0.55, "edgecolor": "none", "pad": 2},
        )
    ax.set_axis_off()
    return fig
