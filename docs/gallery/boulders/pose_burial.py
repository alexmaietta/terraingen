# SPDX-License-Identifier: BSD-3-Clause

"""How deep boulders sit in the regolith: the same boulders of 0.3 m and up on a 30 m square, sized
by Golombek and Rapp's model at 10% rock cover, each buried by a share of its height drawn between
none and none, 0.1 and 0.4 (the default), and 0.5 and 0.9, drawn to scale and colored by the height
they stand above the ground. Bottom: how many stand at least each height, the measure a rover's
clearance meets."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True

HALF_SIZE_M = 15.0
SETTINGS = [(0.0, 0.0), (0.1, 0.4), (0.5, 0.9)]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def label(burial):
    """The text for a burial range."""
    return f"burial {burial[0]:g} to {burial[1]:g}"


def figure(quick=False):
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)

    fig = plt.figure(figsize=(16, 10.5), layout="constrained")
    grid = fig.add_gridspec(2, len(SETTINGS), height_ratios=[1, 0.7])
    map_axes = [fig.add_subplot(grid[0, column]) for column in range(len(SETTINGS))]
    count_ax = fig.add_subplot(grid[1, :])
    for ax, setting, color in zip(map_axes, SETTINGS, COLORS, strict=True):
        options = terraingen.BoulderLayerOptions(seed=4)
        sizes = options.sizes
        sizes.distribution = None
        sizes.coverage = 0.1
        sizes.min_diameter = 0.3
        options.sizes = sizes
        pose = options.pose
        pose.min_burial, pose.max_burial = setting
        options.pose = pose
        table = terraingen.BoulderLayer(options).boulders(region).table
        exposed = np.asarray(table["height"]) * (1.0 - np.asarray(table["burial"]))
        circles = gallery.show_boulders(
            ax, table, region, exposed, cmap="magma_r", vmin=0.0, vmax=0.8, colorbar=False
        )
        ax.set_title(label(setting), color=color)
        heights = np.sort(exposed)[::-1]
        count_ax.semilogy(heights, np.arange(1, len(heights) + 1), color=color, lw=1.6, label=label(setting))
    fig.colorbar(circles, ax=map_axes, label="Height above the ground (m)", shrink=0.85)
    count_ax.set_xlabel("Height above the ground (m)")
    count_ax.set_ylabel("Boulders at least this high")
    count_ax.legend()
    return fig
