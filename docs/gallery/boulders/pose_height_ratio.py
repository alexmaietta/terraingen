# SPDX-License-Identifier: BSD-3-Clause

"""How tall boulders are for their width. The same boulders of 0.3 m and up on a 30 m square, sized
by Golombek and Rapp's model at 10% rock cover, have heights drawn between 0.2 and 0.3 of their
diameters (flat slabs), 0.4 and 0.6 (the default) and 0.7 and 0.9 (near-round blocks), each buried
by the default 0.1 to 0.4 of its height. They are drawn to scale and colored by the height they
stand above the ground. The bottom plot shows how many stand at least each height."""

import gallery
import matplotlib.pyplot as plt
import numpy as np

import terraingen

WIDE = True
SECTION = "Pose"

HALF_SIZE_M = 15.0
SETTINGS = [(0.2, 0.3), (0.4, 0.6), (0.7, 0.9)]
COLORS = ["#1f77b4", "#d62728", "#2ca02c"]


def label(ratio):
    """The text for a height ratio range."""
    return f"height {ratio[0]:g} to {ratio[1]:g} D"


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
        pose.min_height_ratio, pose.max_height_ratio = setting
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
