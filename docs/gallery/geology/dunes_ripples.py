# SPDX-License-Identifier: BSD-3-Clause

"""Wind ripples on a Martian barchan, 2.1 m apart and 0.2 m high, as Lapotre et al. (2016) measured.
The left panel shows the barchan, 2.4 km across, at 6 m, too coarse for its ripples. The others show
the boxed 100 m of its windward flank at 0.2 m, hillshaded. It is bare, then has ripples running
straight across the wind, then has the dune's slope bending them around the flank and packing them
tighter where it steepens."""

import gallery
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Rectangle

import terraingen

WIDE = True
SECTION = "Dunes and bedforms"

FIELD_M = 2400.0
WINDOW_M = 100.0


def dune_options(ripples):
    options = terraingen.mars_barchans()
    options.seed = 2
    options.ripples = ripples
    return options


def figure(quick=False):
    coarse = 24.0 if quick else 6.0
    fine = 0.8 if quick else 0.2
    size = int(FIELD_M / coarse) + 1
    field = terraingen.TerrainGenerator(
        terraingen.FlatSource(size, size, coarse), [terraingen.DuneLayer(dune_options(None))]
    )
    heights = terraingen.evaluate(field).values
    row, column = np.unravel_index(np.argmax(heights), heights.shape)
    crest = ((column - 0.5 * (size - 1)) * coarse, (0.5 * (size - 1) - row) * coarse)
    center = (crest[0] - 70.0, crest[1] + 60.0)

    straight = terraingen.mars_dune_ripples()
    straight.slope_coupling = 0.0
    panels = [
        ("Inset: bare", None),
        ("Inset: ripples, straight", straight),
        ("Inset: ripples, bent by the slope", terraingen.mars_dune_ripples()),
    ]

    fig, axes = plt.subplots(1, len(panels) + 1, figsize=(18, 4.4), layout="constrained")
    gallery.show_heights(axes[0], heights, coarse, cmap="copper")
    axes[0].add_patch(
        Rectangle(
            (center[0] - 0.5 * WINDOW_M, center[1] - 0.5 * WINDOW_M),
            WINDOW_M,
            WINDOW_M,
            fill=False,
            color="#1f77b4",
            lw=2.0,
        )
    )
    axes[0].annotate(
        "inset",
        (center[0] - 0.5 * WINDOW_M, center[1]),
        xytext=(-60.0, 0.0),
        textcoords="offset points",
        color="#1f77b4",
        va="center",
        arrowprops={"arrowstyle": "->", "color": "#1f77b4"},
    )
    axes[0].set_title("Barchan at 6 m, inset boxed")
    window = terraingen.RasterGrid.centered(center, (WINDOW_M, WINDOW_M), fine)
    for ax, (title, ripples) in zip(axes[1:], panels, strict=True):
        terrain = terraingen.TerrainGenerator(field.source, [terraingen.DuneLayer(dune_options(ripples))])
        gallery.show_hillshade(ax, terraingen.evaluate(terrain, grid=window).values, fine, sun_elevation=25.0)
        ax.add_patch(
            Rectangle(
                (0.0, 0.0),
                1.0,
                1.0,
                transform=ax.transAxes,
                fill=False,
                color="#1f77b4",
                lw=3.0,
                clip_on=False,
            )
        )
        ax.set_title(title)
    return fig
