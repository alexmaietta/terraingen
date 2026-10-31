# SPDX-License-Identifier: BSD-3-Clause

"""Crater spacing with both settings varied together, for fresh craters from 5 m to 60 m at three times
the Trask equilibrium on 300 m squares of fractal relief. From left to right, the panels show no
spacing, a light thinning of near-equal craters (ratio 1.2, half a diameter apart), a moderate one
(ratio 2, a diameter apart) and a strong one (ratio 4, 1.25 diameters apart) that leaves an evenly
spread field."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Spacing"

SIZE_M = 300.0
SETTINGS = [None, (1.2, 0.5), (2.0, 1.0), (4.0, 1.25)]


def figure(quick=False):
    pixel = 2.0 if quick else 0.5
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    models = terraingen.CraterModels(
        terraingen.PowerLawSfd(3.0 * 10.0**-1.1, 2.0),
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
    )

    fig, axes = plt.subplots(1, len(SETTINGS), figsize=(18, 4.9), layout="constrained")
    for ax, setting in zip(axes, SETTINGS, strict=True):
        generation = terraingen.CraterGenerationOptions(min_diameter=5.0, max_diameter=60.0)
        if setting is not None:
            ratio, separation = setting
            generation.spacing = terraingen.CraterSpacing(diameter_ratio=ratio, separation=separation)
        craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=4, generation=generation))
        terrain = terraingen.Terrain(
            source=terraingen.FlatSource(size, size, pixel), layers=[relief, craters]
        )
        count = len(terrain.craters().table["diameter"])
        gallery.show_hillshade(ax, terrain.evaluate().values, pixel)
        if setting is None:
            ax.set_title(f"No spacing: {count} craters")
        else:
            ax.set_title(f"ratio {setting[0]:g}, separation {setting[1]:g}: {count} craters")
    return fig
