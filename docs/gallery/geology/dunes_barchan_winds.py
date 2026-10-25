# SPDX-License-Identifier: BSD-3-Clause

"""Barchan fields on 1 km squares under a wind blowing east, colored by height. Left to right: every
dune facing the same wind; each dune's wind turned by up to 30 and 90 degrees, as on a field where
winds shift; and dune widths spread by 80% around their mean instead of 30%."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True

SIZE_M = 1000.0
PANELS = [
    ("One wind", 0.0, 0.3),
    ("Winds within 30 degrees", 30.0, 0.3),
    ("Winds within 90 degrees", 90.0, 0.3),
    ("Widths spread 80%", 0.0, 0.8),
]


def figure(quick=False):
    pixel = 8.0 if quick else 1.5
    size = int(SIZE_M / pixel) + 1

    fig, axes = plt.subplots(1, len(PANELS), figsize=(18, 4), layout="constrained")
    for ax, (title, spread, width_spread) in zip(axes, PANELS, strict=True):
        options = terraingen.DuneOptions(seed=3, azimuth_spread_degrees=spread, width_spread=width_spread)
        terrain = terraingen.TerrainGenerator(
            terraingen.FlatSource(size, size, pixel), [terraingen.DuneLayer(options)]
        )
        gallery.show_heights(ax, terraingen.evaluate(terrain).values, pixel, cmap="copper")
        ax.set_title(title)
    return fig
