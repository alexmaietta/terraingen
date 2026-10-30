# SPDX-License-Identifier: BSD-3-Clause

"""Crater spacing by separation, for fresh craters from 5 m to 60 m at three times the Trask equilibrium
on 300 m squares of fractal relief, with any two within a size ratio of 2 kept at least 0, 0.5, 1
and 1.25 diameters apart, the larger or younger one kept, until the field spreads evenly."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Spacing"

SIZE_M = 300.0
RATIO = 2.0
SEPARATIONS = [0.0, 0.5, 1.0, 1.25]


def figure(quick=False):
    pixel = 2.0 if quick else 0.5
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    models = terraingen.CraterModels(
        terraingen.PowerLawSfd(3.0 * 10.0**-1.1, 2.0),
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
    )

    fig, axes = plt.subplots(1, len(SEPARATIONS), figsize=(18, 4.9), layout="constrained")
    for ax, separation in zip(axes, SEPARATIONS, strict=True):
        generation = terraingen.CraterGenerationOptions(d_min=5.0, d_max=60.0)
        generation.spacing = terraingen.CraterSpacing(diameter_ratio=RATIO, separation=separation)
        craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=4, generation=generation))
        terrain = terraingen.Terrain(
            source=terraingen.FlatSource(size, size, pixel), layers=[relief, craters]
        )
        count = len(terrain.craters().table["diameter"])
        gallery.show_hillshade(ax, terrain.evaluate().values, pixel)
        ax.set_title(f"ratio {RATIO:g}, separation {separation:g}: {count} craters")
    return fig
