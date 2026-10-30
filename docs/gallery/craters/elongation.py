# SPDX-License-Identifier: BSD-3-Clause

"""Elongated craters, as low-angle impacts make them. Nine identical young 150 m craters are stretched
from round (ellipticity 1) to 1.6 times as long as they are wide, all along a heading of 30 degrees
and with round rims otherwise, on fractal relief."""

import gallery
import matplotlib.pyplot as plt

import terraingen

SECTION = "Rim shapes"

ELLIPTICITY = [1.0, 1.05, 1.1, 1.15, 1.2, 1.3, 1.4, 1.5, 1.6]
DIAMETER = 150.0
SPACING = 240.0


def elongated(ellipticity):
    """Apply options stretching every crater, manual ones included, by `ellipticity`."""
    apply = terraingen.CraterApplyOptions()
    irregularity = apply.irregularity
    irregularity.enabled = ellipticity > 1.0
    irregularity.seed = 3
    irregularity.rough_share = 0.0
    irregularity.elongated_share = 1.0
    irregularity.ellipticity = terraingen.ValueDistribution.constant(ellipticity)
    irregularity.heading_deg = 30.0
    selection = irregularity.selection
    selection.manual = True
    irregularity.selection = selection
    apply.irregularity = irregularity
    return apply


def figure(quick=False):
    pixel = 4.0 if quick else 0.75
    size = int(3 * SPACING / pixel) + 1

    layers = [
        terraingen.CraterLayer.manual(
            [terraingen.Crater(x, y, DIAMETER, age_gyr=0.01)], apply=elongated(ellipticity)
        )
        for (x, y), ellipticity in zip(gallery.grid_centers(SPACING), ELLIPTICITY, strict=True)
    ]
    relief = terraingen.NoiseLayer(seed=2, wavelength=400.0, amplitude=12.0)
    terrain = terraingen.Terrain(source=terraingen.FlatSource(size, size, pixel), layers=[relief, *layers])
    heights = terrain.evaluate().values

    fig, ax = plt.subplots(figsize=(7, 7), layout="constrained")
    gallery.show_crater_grid(ax, heights, pixel, [f"ellipticity {e:g}" for e in ELLIPTICITY], SPACING)
    return fig
