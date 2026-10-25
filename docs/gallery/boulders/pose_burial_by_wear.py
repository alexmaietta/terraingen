# SPDX-License-Identifier: BSD-3-Clause

"""Older boulders sink deeper: boulders of 0.3 m and up on a 30 m square at 10% rock cover, between
craters so their wear spans every stage, buried between none and 0.8 of their height with wear
setting none, half (0.5) and all (1) of the draw. Drawn to scale and colored by their buried share.
Bottom: buried share against wear, from scattered to tracking wear."""

import gallery
import matplotlib.pyplot as plt

import terraingen

WIDE = True

HALF_SIZE_M = 15.0
SETTINGS = [0.0, 0.5, 1.0]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def figure(quick=False):
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)

    fig = plt.figure(figsize=(16, 10.5), layout="constrained")
    grid = fig.add_gridspec(2, len(SETTINGS), height_ratios=[1, 0.7])
    map_axes = [fig.add_subplot(grid[0, column]) for column in range(len(SETTINGS))]
    scatter_ax = fig.add_subplot(grid[1, :])
    for ax, setting, color in zip(map_axes, SETTINGS, COLORS, strict=True):
        options = terraingen.BoulderLayerOptions(seed=4)
        sizes = options.sizes
        sizes.distribution = None
        sizes.coverage = 0.1
        sizes.min_diameter = 0.3
        options.sizes = sizes
        pose = options.pose
        pose.min_burial = 0.0
        pose.max_burial = 0.8
        pose.burial_by_wear = setting
        options.pose = pose
        table = terraingen.BoulderLayer(options).boulders(region).table
        circles = gallery.show_boulders(
            ax, table, region, table["burial"], cmap="viridis", vmin=0.0, vmax=0.8, colorbar=False
        )
        ax.set_title(f"burial_by_wear {setting:g}", color=color)
        scatter_ax.scatter(
            table["wear"], table["burial"], s=6, color=color, alpha=0.6, label=f"burial_by_wear {setting:g}"
        )
    fig.colorbar(circles, ax=map_axes, label="Buried share of the height", shrink=0.85)
    scatter_ax.set_xlabel("Wear (0 fresh, 1 gone)")
    scatter_ax.set_ylabel("Buried share of the height")
    scatter_ax.legend(markerscale=3)
    return fig
