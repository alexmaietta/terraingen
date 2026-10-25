# SPDX-License-Identifier: BSD-3-Clause

"""Nine identical craters from 1 Myr to 1.5 Gyr old, worn down by the default degradation model."""

import gallery
import matplotlib.pyplot as plt

import terraingen

SECTION = "Degradation"

AGES_GYR = [0.001, 0.01, 0.05, 0.1, 0.25, 0.5, 0.75, 1.0, 1.5]
DIAMETER = 150.0
SPACING = 240.0


def age_text(age_gyr):
    return f"{age_gyr * 1000:g} Myr" if age_gyr < 1.0 else f"{age_gyr:g} Gyr"


def figure(quick=False):
    pixel = 4.0 if quick else 0.75
    size = int(3 * SPACING / pixel) + 1

    apply = terraingen.CraterApplyOptions()
    irregularity = apply.irregularity
    irregularity.enabled = True
    selection = irregularity.selection
    selection.manual = True
    irregularity.selection = selection
    apply.irregularity = irregularity

    craters = [
        terraingen.Crater(x, y, DIAMETER, age_gyr=age)
        for (x, y), age in zip(gallery.grid_centers(SPACING), AGES_GYR, strict=True)
    ]
    layer = terraingen.CraterLayer.manual(craters, apply=apply)
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief, layer])
    heights = terraingen.evaluate(terrain).values

    fig, ax = plt.subplots(figsize=(7, 7), layout="constrained")
    gallery.show_crater_grid(ax, heights, pixel, [age_text(age) for age in AGES_GYR], SPACING)
    return fig
