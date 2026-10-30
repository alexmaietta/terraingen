# SPDX-License-Identifier: BSD-3-Clause

"""Barchan fields on 1 km squares under a wind blowing east, colored by height. The first panel has
every dune facing the same wind. The next two turn each dune's wind by up to 30 and 90 degrees, as on
a field where winds shift. The last spreads dune widths by 80% around their mean instead of 30%."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True
SECTION = "Dunes and bedforms"

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
        options = terraingen.DuneOptions(seed=3, azimuth_spread_deg=spread, width_spread=width_spread)
        terrain = terraingen.Terrain(
            source=terraingen.FlatSource(size, size, pixel), layers=[terraingen.DuneLayer(options)]
        )
        gallery.show_heights(ax, terrain.evaluate().values, pixel, cmap="copper")
        ax.set_title(title)
    return fig
