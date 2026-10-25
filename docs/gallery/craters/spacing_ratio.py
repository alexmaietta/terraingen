# SPDX-License-Identifier: BSD-3-Clause

"""Crater spacing by size ratio: fresh craters from 5 m to 60 m at three times the Trask equilibrium
on 300 m squares of fractal relief, any two kept a diameter apart when they count as similar. Left
to right: no spacing, then craters within a size ratio of 1.2, 2 and 4 of each other counted as
similar; the wider the ratio, the more pairs are thinned, until small craters no longer sit on
larger ones."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Spacing"

SIZE_M = 300.0
SEPARATION = 1.0
RATIOS = [None, 1.2, 2.0, 4.0]


def figure(quick=False):
    pixel = 2.0 if quick else 0.5
    size = int(SIZE_M / pixel) + 1
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    models = terraingen.CraterModels(
        terraingen.PowerLawSfd(3.0 * 10.0**-1.1, 2.0),
        terraingen.FreshAges(),
        terraingen.NoDegradation(terraingen.lunar_fresh_crater_morphometry),
    )

    fig, axes = plt.subplots(1, len(RATIOS), figsize=(18, 4.9), layout="constrained")
    for ax, ratio in zip(axes, RATIOS, strict=True):
        generation = terraingen.CraterGenerationOptions(d_min=5.0, d_max=60.0)
        if ratio is not None:
            generation.spacing = terraingen.CraterSpacing(diameter_ratio=ratio, separation=SEPARATION)
        craters = terraingen.CraterLayer(models, terraingen.CraterLayerOptions(seed=4, generation=generation))
        terrain = terraingen.TerrainGenerator(terraingen.FlatSource(size, size, pixel), [relief, craters])
        count = len(terraingen.placed_craters(terrain).table["diameter"])
        gallery.show_hillshade(ax, terraingen.evaluate(terrain).values, pixel)
        if ratio is None:
            ax.set_title(f"No spacing: {count} craters")
        else:
            ax.set_title(f"ratio {ratio:g}, separation {SEPARATION:g}: {count} craters")
    return fig
